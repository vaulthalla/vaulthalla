from __future__ import annotations

import json
import os
from typing import Any

from tools.release.changelog.ai.config import (
    AIProviderKind,
    AIReasoningEffort,
    AIStageName,
    AIStructuredMode,
    DEFAULT_AI_DRAFT_MODEL,
    OPENAI_API_KEY_ENV_VAR,
)
from tools.release.changelog.ai.providers.capabilities import (
    ProviderCapabilities,
    build_structured_mode_fallback_chain,
    get_provider_capabilities,
    resolve_request_parameter_capabilities,
    resolve_generation_settings,
)
from tools.release.changelog.ai.providers.parsing import JSONParseError, parse_json_object_from_text

LOCAL_NO_AUTH_API_KEY_PLACEHOLDER = "local-no-auth"
_MODE_RECOVERABLE_ERROR_MARKERS = (
    "response_format",
    "json_schema",
    "invalid schema",
    "strict",
    "unsupported",
    "not supported",
    "invalid request",
    "invalid_request_error",
    "unknown parameter",
    "unrecognized",
    "does not support",
)
_RESPONSES_TEXT_CONTENT_TYPES = frozenset(
    {
        "output_text",
        "text",
        "summary_text",
        "reasoning_text",
    }
)
_RESPONSES_JSON_CONTENT_FIELDS = (
    "json",
    "parsed",
    "output_json",
)
_EVIDENCE_STRING_LIMIT = 4096
_EVIDENCE_LIST_LIMIT = 12
_HOSTED_PROMPT_JSON_MIN_OUTPUT_TOKENS = 1200
_HOSTED_TRIAGE_STRICT_MIN_OUTPUT_TOKENS = 1400
_HOSTED_TRIAGE_PROMPT_JSON_MIN_OUTPUT_TOKENS = 2200
_SECRET_KEY_EXACT_NAMES = {
    "api_key",
    "authorization",
    "proxy_authorization",
    "auth",
    "auth_token",
    "access_token",
    "refresh_token",
    "id_token",
    "secret",
    "password",
}
_SECRET_KEY_SUFFIXES = (
    "_api_key",
    "_auth_token",
    "_access_token",
    "_refresh_token",
    "_authorization",
    "_password",
    "_secret",
)


class _AttemptFailure(Exception):
    def __init__(
        self,
        message: str,
        *,
        transport: str,
        request_payload: dict[str, Any],
        response_received: bool,
        response_summary: dict[str, Any] | None = None,
        request_payload_debug: Any | None = None,
        response_payload_debug: Any | None = None,
        request_payload_raw: Any | None = None,
        response_payload_raw: Any | None = None,
    ) -> None:
        super().__init__(message)
        self.transport = transport
        self.request_payload = request_payload
        self.response_received = response_received
        self.response_summary = response_summary
        self.request_payload_debug = request_payload_debug
        self.response_payload_debug = response_payload_debug
        self.request_payload_raw = request_payload_raw
        self.response_payload_raw = response_payload_raw


class _GenerationOutcome:
    def __init__(
        self,
        *,
        transport: str,
        request_payload: dict[str, Any],
        response_summary: dict[str, Any],
        content: str,
        request_payload_debug: Any,
        response_payload_debug: Any,
        request_payload_raw: Any,
        response_payload_raw: Any,
    ) -> None:
        self.transport = transport
        self.request_payload = request_payload
        self.response_summary = response_summary
        self.content = content
        self.request_payload_debug = request_payload_debug
        self.response_payload_debug = response_payload_debug
        self.request_payload_raw = request_payload_raw
        self.response_payload_raw = response_payload_raw


class OpenAIProvider:
    """Thin OpenAI transport adapter for one structured JSON generation pass."""

    def __init__(
        self,
        *,
        model: str = DEFAULT_AI_DRAFT_MODEL,
        provider_kind: AIProviderKind = "openai",
        api_key: str | None = None,
        api_key_env_var: str = OPENAI_API_KEY_ENV_VAR,
        base_url: str | None = None,
        timeout_seconds: float | None = None,
        require_api_key: bool = True,
        sdk_client: Any | None = None,
    ) -> None:
        self.model = model
        self.provider_kind = provider_kind
        self.base_url = base_url
        self.last_structured_mode_used: AIStructuredMode | None = None
        self._last_generation_evidence: dict[str, Any] | None = None

        if sdk_client is not None:
            self._client = sdk_client
            return

        resolved_api_key = api_key or os.getenv(api_key_env_var)
        if not resolved_api_key and require_api_key:
            raise ValueError(
                f"{api_key_env_var} is not set. Export {api_key_env_var} to run `changelog ai-draft`."
            )
        if not resolved_api_key:
            # OpenAI-compatible local gateways may not require auth; keep a placeholder
            # so SDK client construction still succeeds when no key is provided.
            resolved_api_key = LOCAL_NO_AUTH_API_KEY_PLACEHOLDER

        self._client = _build_sdk_client(
            api_key=resolved_api_key,
            base_url=base_url,
            timeout_seconds=timeout_seconds,
        )

    def list_models(self) -> list[str]:
        try:
            response = self._client.models.list()
        except Exception as exc:
            raise ValueError(f"Model discovery request failed: {exc}") from exc

        data = response.get("data") if isinstance(response, dict) else getattr(response, "data", None)
        if not isinstance(data, list):
            raise ValueError("Model discovery response did not include a valid `data` list.")

        model_ids: list[str] = []
        for item in data:
            model_id = item.get("id") if isinstance(item, dict) else getattr(item, "id", None)
            if isinstance(model_id, str) and model_id.strip():
                model_ids.append(model_id.strip())
        return sorted(set(model_ids))

    def failure_evidence_snapshot(self) -> dict[str, Any] | None:
        if self._last_generation_evidence is None:
            return None
        return _safe_json_clone(self._last_generation_evidence)

    def generate_structured_json(
        self,
        *,
        stage: AIStageName | None = None,
        system_prompt: str,
        user_prompt: str,
        json_schema: dict[str, Any],
        reasoning_effort: AIReasoningEffort | None = None,
        structured_mode: AIStructuredMode | None = None,
        temperature: float | None = None,
        max_output_tokens: int | None = None,
    ) -> dict[str, Any]:
        self.last_structured_mode_used = None
        self._last_generation_evidence = None
        settings = resolve_generation_settings(
            provider_kind=self.provider_kind,
            requested_structured_mode=structured_mode,
            requested_reasoning_effort=reasoning_effort,
        )
        initial_mode_chain = build_structured_mode_fallback_chain(settings.structured_mode)
        mode_chain = _resolve_runtime_mode_chain(
            provider_kind=self.provider_kind,
            model=self.model,
            stage=stage,
            mode_chain=initial_mode_chain,
        )
        trace: dict[str, Any] = {
            "provider_kind": self.provider_kind,
            "model": self.model,
            "base_url": self.base_url,
            "requested": {
                "stage": stage,
                "structured_mode": structured_mode,
                "reasoning_effort": reasoning_effort,
                "temperature": temperature,
                "max_output_tokens": max_output_tokens,
            },
            "resolved_settings": {
                "structured_mode": settings.structured_mode,
                "reasoning_effort": settings.reasoning_effort,
                "degradations": list(settings.degradations),
            },
            "requested_mode_chain": list(initial_mode_chain),
            "mode_chain": list(mode_chain),
            "attempts": [],
        }
        if mode_chain != initial_mode_chain:
            if _is_hosted_gpt5_model(provider_kind=self.provider_kind, model=self.model) and stage == "triage":
                trace["resolved_settings"]["degradations"].append(
                    "Hosted OpenAI GPT-5 triage prefers answer-first `prompt_json` before strict schema and skips `json_object` fallback."
                )
            else:
                trace["resolved_settings"]["degradations"].append(
                    "Hosted OpenAI GPT-5 runtime skips json_object fallback because it repeatedly returns reasoning-only output in this pipeline."
                )
        errors: list[str] = []

        for mode_index, mode in enumerate(mode_chain):
            attempt_reasoning_effort = _resolve_attempt_reasoning_effort(
                provider_kind=self.provider_kind,
                model=self.model,
                stage=stage,
                structured_mode=mode,
                requested_reasoning_effort=settings.reasoning_effort,
            )
            attempt_max_output_tokens = _resolve_attempt_max_output_tokens(
                provider_kind=self.provider_kind,
                model=self.model,
                stage=stage,
                structured_mode=mode,
                requested_max_output_tokens=max_output_tokens,
            )
            attempt: dict[str, Any] = {
                "attempt_index": mode_index + 1,
                "mode": mode,
                "normalized_request": {
                    "structured_mode": mode,
                    "reasoning_effort": attempt_reasoning_effort,
                    "temperature": temperature,
                    "max_output_tokens": attempt_max_output_tokens,
                },
            }
            trace["attempts"].append(attempt)
            try:
                outcome = self._generate_text_output(
                    system_prompt=system_prompt,
                    user_prompt=user_prompt,
                    json_schema=json_schema,
                    structured_mode=mode,
                    reasoning_effort=attempt_reasoning_effort,
                    temperature=temperature,
                    max_output_tokens=attempt_max_output_tokens,
                )
                attempt["transport"] = outcome.transport
                attempt["response_received"] = True
                attempt["request_summary"] = _summarize_request_payload(
                    request_payload=outcome.request_payload,
                    transport=outcome.transport,
                )
                attempt["response_summary"] = outcome.response_summary
                attempt["response_payload_debug"] = outcome.response_payload_debug
                attempt["request_payload_debug"] = outcome.request_payload_debug
                attempt["request_payload_raw"] = outcome.request_payload_raw
                attempt["response_payload_raw"] = outcome.response_payload_raw
                attempt["content_preview"] = _truncate_text(outcome.content)
                attempt["content_length"] = len(outcome.content)
                try:
                    parsed = parse_json_object_from_text(outcome.content)
                except JSONParseError as exc:
                    attempt["error"] = str(exc)
                    attempt["error_type"] = "json_parse_error"
                    attempt["parse_candidates"] = [
                        _truncate_text(candidate)
                        for candidate in exc.diagnostics.candidates[:_EVIDENCE_LIST_LIMIT]
                    ]
                    attempt["parse_candidates_lengths"] = [
                        len(candidate) for candidate in exc.diagnostics.candidates[:_EVIDENCE_LIST_LIMIT]
                    ]
                    attempt["parse_failures"] = [
                        {
                            "reason": failure.reason,
                            "sample": failure.sample,
                        }
                        for failure in exc.diagnostics.failures[:_EVIDENCE_LIST_LIMIT]
                    ]
                    attempt["likely_truncated_json"] = _detect_likely_truncated_json(exc)
                    raise

                self.last_structured_mode_used = mode
                attempt["result"] = "success"
                trace["successful_mode"] = mode
                trace["parsed_json_preview"] = _sanitize_for_evidence(parsed)
                trace["status"] = "success"
                self._last_generation_evidence = trace
                return parsed
            except _AttemptFailure as exc:
                message = str(exc)
                attempt["error"] = message
                attempt["error_type"] = "provider_error"
                attempt["transport"] = exc.transport
                attempt["response_received"] = exc.response_received
                attempt["request_summary"] = _summarize_request_payload(
                    request_payload=exc.request_payload,
                    transport=exc.transport,
                )
                attempt["response_summary"] = exc.response_summary
                attempt["response_payload_debug"] = exc.response_payload_debug
                attempt["request_payload_debug"] = exc.request_payload_debug
                attempt["request_payload_raw"] = exc.request_payload_raw
                attempt["response_payload_raw"] = exc.response_payload_raw
            except Exception as exc:
                message = str(exc)
                attempt["error"] = message
                attempt["error_type"] = "unknown"
                mode_label = f"{self.provider_kind}:{mode}"
                errors.append(f"{mode_label}: {message}")
                if message.startswith("AI provider refusal:"):
                    trace["status"] = "failed"
                    trace["final_error"] = message
                    self._last_generation_evidence = trace
                    raise ValueError(message) from exc
                has_next_mode = mode_index < len(mode_chain) - 1
                if has_next_mode and _is_mode_recoverable_error(message):
                    continue
                break
            else:
                message = ""

            mode_label = f"{self.provider_kind}:{mode}"
            errors.append(f"{mode_label}: {message}")
            has_next_mode = mode_index < len(mode_chain) - 1
            if has_next_mode and _is_mode_recoverable_error(message):
                continue
            break

        attempted = ", ".join(mode_chain)
        details = " | ".join(errors[-2:])
        trace["status"] = "failed"
        trace["final_error"] = (
            f"AI generation failed after structured-mode attempts [{attempted}]. {details}"
        )
        self._last_generation_evidence = trace
        raise ValueError(
            f"AI generation failed after structured-mode attempts [{attempted}]. {details}"
        )

    def capabilities(self) -> ProviderCapabilities:
        return get_provider_capabilities(self.provider_kind)

    def _generate_text_output(
        self,
        *,
        system_prompt: str,
        user_prompt: str,
        json_schema: dict[str, Any],
        structured_mode: AIStructuredMode,
        reasoning_effort: AIReasoningEffort | None,
        temperature: float | None,
        max_output_tokens: int | None,
    ) -> _GenerationOutcome:
        if self.provider_kind == "openai":
            return self._generate_hosted_openai_output(
                system_prompt=system_prompt,
                user_prompt=user_prompt,
                json_schema=json_schema,
                structured_mode=structured_mode,
                reasoning_effort=reasoning_effort,
                temperature=temperature,
                max_output_tokens=max_output_tokens,
            )
        return self._generate_openai_compatible_output(
            system_prompt=system_prompt,
            user_prompt=user_prompt,
            json_schema=json_schema,
            structured_mode=structured_mode,
            reasoning_effort=reasoning_effort,
            temperature=temperature,
            max_output_tokens=max_output_tokens,
        )

    def _generate_hosted_openai_output(
        self,
        *,
        system_prompt: str,
        user_prompt: str,
        json_schema: dict[str, Any],
        structured_mode: AIStructuredMode,
        reasoning_effort: AIReasoningEffort | None,
        temperature: float | None,
        max_output_tokens: int | None,
    ) -> _GenerationOutcome:
        responses = getattr(self._client, "responses", None)
        create = getattr(responses, "create", None)
        if not callable(create):
            raise ValueError(
                "AI transport error: Hosted OpenAI provider requires Responses API support on the active OpenAI SDK/client."
            )
        parameter_capabilities = resolve_request_parameter_capabilities(
            provider_kind="openai",
            model=self.model,
        )
        request_payload = self._build_responses_request(
            model=self.model,
            system_prompt=system_prompt,
            user_prompt=user_prompt,
            json_schema=json_schema,
            structured_mode=structured_mode,
            reasoning_effort=reasoning_effort,
            temperature=temperature,
            max_output_tokens=max_output_tokens,
            include_temperature=parameter_capabilities.supports_temperature,
        )
        request_payload_raw = _serialize_payload_for_failure_capture(request_payload)
        try:
            response = create(**request_payload)
        except Exception as exc:
            raise _AttemptFailure(
                f"AI transport error: Responses API request failed: {exc}",
                transport="responses",
                request_payload=request_payload,
                response_received=False,
                request_payload_debug=_sanitize_for_evidence(request_payload),
                request_payload_raw=request_payload_raw,
            ) from exc

        response_summary = _summarize_responses_response(response)
        response_payload_raw = _serialize_payload_for_failure_capture(response)
        try:
            content = _extract_responses_output_text(response)
        except Exception as exc:
            raise _AttemptFailure(
                str(exc),
                transport="responses",
                request_payload=request_payload,
                response_received=True,
                response_summary=response_summary,
                request_payload_debug=_sanitize_for_evidence(request_payload),
                response_payload_debug=_sanitize_for_evidence(response),
                request_payload_raw=request_payload_raw,
                response_payload_raw=response_payload_raw,
            ) from exc
        return _GenerationOutcome(
            transport="responses",
            request_payload=request_payload,
            response_summary=response_summary,
            content=content,
            request_payload_debug=_sanitize_for_evidence(request_payload),
            response_payload_debug=_sanitize_for_evidence(response),
            request_payload_raw=request_payload_raw,
            response_payload_raw=response_payload_raw,
        )

    def _generate_openai_compatible_output(
        self,
        *,
        system_prompt: str,
        user_prompt: str,
        json_schema: dict[str, Any],
        structured_mode: AIStructuredMode,
        reasoning_effort: AIReasoningEffort | None,
        temperature: float | None,
        max_output_tokens: int | None,
    ) -> _GenerationOutcome:
        request_payload = self._build_chat_request(
            model=self.model,
            system_prompt=system_prompt,
            user_prompt=user_prompt,
            json_schema=json_schema,
            structured_mode=structured_mode,
            reasoning_effort=reasoning_effort,
            temperature=temperature,
            max_output_tokens=max_output_tokens,
        )
        request_payload_raw = _serialize_payload_for_failure_capture(request_payload)
        try:
            response = self._client.chat.completions.create(**request_payload)
        except Exception as exc:
            raise _AttemptFailure(
                f"AI transport error: Chat Completions request failed: {exc}",
                transport="chat_completions",
                request_payload=request_payload,
                response_received=False,
                request_payload_debug=_sanitize_for_evidence(request_payload),
                request_payload_raw=request_payload_raw,
            ) from exc

        response_summary = _summarize_chat_response(response)
        response_payload_raw = _serialize_payload_for_failure_capture(response)
        try:
            content = _extract_message_content(response)
        except Exception as exc:
            raise _AttemptFailure(
                str(exc),
                transport="chat_completions",
                request_payload=request_payload,
                response_received=True,
                response_summary=response_summary,
                request_payload_debug=_sanitize_for_evidence(request_payload),
                response_payload_debug=_sanitize_for_evidence(response),
                request_payload_raw=request_payload_raw,
                response_payload_raw=response_payload_raw,
            ) from exc
        return _GenerationOutcome(
            transport="chat_completions",
            request_payload=request_payload,
            response_summary=response_summary,
            content=content,
            request_payload_debug=_sanitize_for_evidence(request_payload),
            response_payload_debug=_sanitize_for_evidence(response),
            request_payload_raw=request_payload_raw,
            response_payload_raw=response_payload_raw,
        )

    @staticmethod
    def _build_chat_request(
        *,
        model: str,
        system_prompt: str,
        user_prompt: str,
        json_schema: dict[str, Any],
        structured_mode: AIStructuredMode,
        reasoning_effort: AIReasoningEffort | None,
        temperature: float | None,
        max_output_tokens: int | None,
    ) -> dict[str, Any]:
        messages = [
            {"role": "system", "content": system_prompt},
            {"role": "user", "content": user_prompt},
        ]
        request: dict[str, Any] = {
            "model": model,
            "messages": messages,
        }

        if structured_mode == "strict_json_schema":
            request["response_format"] = {
                "type": "json_schema",
                "json_schema": {
                    "name": "vaulthalla_release_changelog_draft",
                    "schema": json_schema,
                    "strict": True,
                },
            }
        elif structured_mode == "json_object":
            request["response_format"] = {"type": "json_object"}
        else:
            messages[0]["content"] = (
                f"{system_prompt}\n\nReturn only a valid JSON object with no prose or markdown."
            )

        if reasoning_effort is not None:
            request["reasoning"] = {"effort": reasoning_effort}
        if temperature is not None:
            request["temperature"] = temperature
        if max_output_tokens is not None:
            request["max_completion_tokens"] = max_output_tokens

        return request

    @staticmethod
    def _build_responses_request(
        *,
        model: str,
        system_prompt: str,
        user_prompt: str,
        json_schema: dict[str, Any],
        structured_mode: AIStructuredMode,
        reasoning_effort: AIReasoningEffort | None,
        temperature: float | None,
        max_output_tokens: int | None,
        include_temperature: bool,
    ) -> dict[str, Any]:
        effective_system_prompt = system_prompt
        if structured_mode == "prompt_json":
            effective_system_prompt = (
                f"{system_prompt}\n\nReturn only a valid JSON object with no prose or markdown."
            )

        request: dict[str, Any] = {
            "model": model,
            "input": [
                {
                    "role": "developer",
                    "content": [{"type": "input_text", "text": effective_system_prompt}],
                },
                {
                    "role": "user",
                    "content": [{"type": "input_text", "text": user_prompt}],
                },
            ],
        }

        if structured_mode == "strict_json_schema":
            request["text"] = {
                "format": {
                    "type": "json_schema",
                    "name": "vaulthalla_release_changelog_draft",
                    "schema": json_schema,
                    "strict": True,
                }
            }
        elif structured_mode == "json_object":
            request["text"] = {"format": {"type": "json_object"}}

        if reasoning_effort is not None:
            request["reasoning"] = {"effort": reasoning_effort}
        if include_temperature and temperature is not None:
            request["temperature"] = temperature
        if max_output_tokens is not None:
            request["max_output_tokens"] = max_output_tokens

        return request


def _build_sdk_client(
    *,
    api_key: str,
    base_url: str | None = None,
    timeout_seconds: float | None = None,
) -> Any:
    try:
        from openai import OpenAI
    except Exception as exc:
        raise ValueError(
            "OpenAI SDK is not available in this environment. Install `openai` in the active venv."
        ) from exc
    kwargs: dict[str, Any] = {"api_key": api_key}
    if base_url:
        kwargs["base_url"] = base_url
    if timeout_seconds is not None:
        kwargs["timeout"] = timeout_seconds
    return OpenAI(**kwargs)


def _extract_message_content(response: Any) -> str:
    choices = getattr(response, "choices", None)
    if not choices:
        raise ValueError("AI parse error: OpenAI response contained no choices.")

    message = getattr(choices[0], "message", None)
    if message is None:
        raise ValueError("AI parse error: OpenAI response choice contained no message.")

    refusal = getattr(message, "refusal", None)
    if refusal:
        raise ValueError(f"AI provider refusal: {refusal}")

    content = getattr(message, "content", None)
    if isinstance(content, str) and content.strip():
        return content

    # Some SDK variants may return content blocks instead of a raw string.
    if isinstance(content, list):
        blocks: list[str] = []
        for block in content:
            if isinstance(block, dict):
                text = block.get("text")
            else:
                text = getattr(block, "text", None)
            if isinstance(text, str) and text.strip():
                blocks.append(text)
        if blocks:
            return "".join(blocks)

    raise ValueError("AI parse error: OpenAI response did not include structured JSON content.")


def _extract_responses_output_text(response: Any) -> str:
    output_text = _extract_text_candidate(_read_response_field(response, "output_text"))
    if output_text is not None:
        return output_text

    response_status = _response_type_label(_read_response_field(response, "status"))
    incomplete_details = _read_response_field(response, "incomplete_details")
    incomplete_reason = _response_type_label(
        _read_response_field(incomplete_details, "reason")
    )
    output_items = _as_response_items(_read_response_field(response, "output"))
    text_chunks: list[str] = []
    text_seen: set[str] = set()
    json_chunks: list[str] = []
    json_seen: set[str] = set()
    refusal_chunks: list[str] = []
    item_types: set[str] = set()
    content_types: set[str] = set()

    for item in output_items:
        item_type = _response_type_label(_read_response_field(item, "type"))
        if item_type is not None:
            item_types.add(item_type)

        item_refusal = _extract_text_candidate(_read_response_field(item, "refusal"))
        if item_type == "refusal" or item_refusal is not None:
            refusal_chunks.append(item_refusal or "request refused")

        _append_unique(
            text_chunks,
            text_seen,
            _extract_text_candidate(_read_response_field(item, "text")),
        )
        _append_unique(
            text_chunks,
            text_seen,
            _extract_text_candidate(_read_response_field(item, "output_text")),
        )
        for field in _RESPONSES_JSON_CONTENT_FIELDS:
            _append_unique(
                json_chunks,
                json_seen,
                _extract_json_candidate(_read_response_field(item, field)),
            )

        for content_item in _as_response_items(_read_response_field(item, "content")):
            content_type = _response_type_label(_read_response_field(content_item, "type"))
            if content_type is not None:
                content_types.add(content_type)

            content_refusal = _extract_text_candidate(_read_response_field(content_item, "refusal"))
            if content_type == "refusal" or content_refusal is not None:
                refusal_chunks.append(content_refusal or "request refused")

            _append_unique(
                text_chunks,
                text_seen,
                _extract_text_candidate(_read_response_field(content_item, "text")),
            )
            _append_unique(
                text_chunks,
                text_seen,
                _extract_text_candidate(_read_response_field(content_item, "output_text")),
            )
            _append_unique(
                text_chunks,
                text_seen,
                _extract_text_candidate(_read_response_field(content_item, "summary")),
            )
            _append_unique(
                text_chunks,
                text_seen,
                _extract_text_candidate(_read_response_field(content_item, "reasoning")),
            )
            if content_type in _RESPONSES_TEXT_CONTENT_TYPES:
                _append_unique(
                    text_chunks,
                    text_seen,
                    _extract_text_candidate(content_item),
                )

            for field in _RESPONSES_JSON_CONTENT_FIELDS:
                _append_unique(
                    json_chunks,
                    json_seen,
                    _extract_json_candidate(_read_response_field(content_item, field)),
                )

    if text_chunks:
        return "".join(text_chunks)
    if json_chunks:
        return json_chunks[0]

    refusal_only = bool(refusal_chunks)
    if refusal_only:
        refusal_text = " | ".join(chunk for chunk in refusal_chunks if chunk.strip())
        raise ValueError(f"AI provider refusal: {refusal_text or 'request refused'}")

    raise ValueError(
        _format_responses_parse_error(
            item_types=item_types,
            content_types=content_types,
            saw_refusal=bool(refusal_chunks),
            saw_output=bool(output_items),
            response_status=response_status,
            incomplete_reason=incomplete_reason,
        )
    )


def _read_response_field(obj: Any, key: str) -> Any:
    if isinstance(obj, dict):
        return obj.get(key)
    return getattr(obj, key, None)


def _as_response_items(value: Any) -> list[Any]:
    if value is None:
        return []
    if isinstance(value, (list, tuple)):
        return list(value)
    return [value]


def _response_type_label(value: Any) -> str | None:
    if value is None:
        return None
    if isinstance(value, str):
        label = value.strip()
        return label or None
    return str(value)


def _extract_text_candidate(value: Any, *, depth: int = 0) -> str | None:
    if value is None or depth > 5:
        return None
    if isinstance(value, str):
        text = value.strip()
        return text if text else None
    if isinstance(value, (list, tuple)):
        chunks = [
            chunk
            for chunk in (_extract_text_candidate(item, depth=depth + 1) for item in value)
            if chunk is not None
        ]
        if chunks:
            return "".join(chunks)
        return None
    if isinstance(value, dict):
        for key in (
            "value",
            "text",
            "output_text",
            "summary_text",
            "reasoning_text",
            "summary",
            "reasoning",
            "content",
            "message",
        ):
            nested = value.get(key)
            extracted = _extract_text_candidate(nested, depth=depth + 1)
            if extracted is not None:
                return extracted
        return None

    for attr in (
        "value",
        "text",
        "output_text",
        "summary_text",
        "reasoning_text",
        "summary",
        "reasoning",
        "content",
        "message",
    ):
        nested = getattr(value, attr, None)
        if nested is None:
            continue
        extracted = _extract_text_candidate(nested, depth=depth + 1)
        if extracted is not None:
            return extracted
    return None


def _extract_json_candidate(value: Any) -> str | None:
    if value is None:
        return None
    if isinstance(value, (dict, list)):
        return json.dumps(value, ensure_ascii=False)
    model_dump = getattr(value, "model_dump", None)
    if callable(model_dump):
        try:
            dumped = model_dump(exclude_none=True)
        except TypeError:
            dumped = model_dump()
        if isinstance(dumped, (dict, list)):
            return json.dumps(dumped, ensure_ascii=False)
    return None


def _append_unique(buffer: list[str], seen: set[str], value: str | None) -> None:
    if value is None:
        return
    if value in seen:
        return
    seen.add(value)
    buffer.append(value)


def _format_responses_parse_error(
    *,
    item_types: set[str],
    content_types: set[str],
    saw_refusal: bool,
    saw_output: bool,
    response_status: str | None,
    incomplete_reason: str | None,
) -> str:
    item_types_label = ", ".join(sorted(item_types)) if item_types else "<none>"
    content_types_label = ", ".join(sorted(content_types)) if content_types else "<none>"
    response_status_label = response_status or "<none>"
    incomplete_reason_label = incomplete_reason or "<none>"
    return (
        "AI parse error: Responses API output had no extractable text or JSON content "
        f"(saw_output={saw_output}, item_types={item_types_label}, "
        f"content_types={content_types_label}, saw_refusal={saw_refusal}, "
        f"response_status={response_status_label}, incomplete_reason={incomplete_reason_label})."
    )


def _resolve_runtime_mode_chain(
    *,
    provider_kind: AIProviderKind,
    model: str,
    stage: AIStageName | None,
    mode_chain: tuple[AIStructuredMode, ...],
) -> tuple[AIStructuredMode, ...]:
    if not _is_hosted_gpt5_model(provider_kind=provider_kind, model=model):
        return mode_chain
    filtered = tuple(mode for mode in mode_chain if mode != "json_object")
    if not filtered:
        return mode_chain
    if stage != "triage":
        return filtered

    # For hosted GPT-5 triage, lead with prompt_json to maximize answer-start probability.
    ordered: list[AIStructuredMode] = []
    if "prompt_json" in filtered:
        ordered.append("prompt_json")
    if "strict_json_schema" in filtered:
        ordered.append("strict_json_schema")
    return tuple(ordered) if ordered else filtered


def _resolve_attempt_reasoning_effort(
    *,
    provider_kind: AIProviderKind,
    model: str,
    stage: AIStageName | None,
    structured_mode: AIStructuredMode,
    requested_reasoning_effort: AIReasoningEffort | None,
) -> AIReasoningEffort | None:
    if _is_hosted_gpt5_model(provider_kind=provider_kind, model=model) and stage == "triage":
        # For hosted GPT-5 triage, enforce explicit low reasoning to avoid opaque default effort.
        return requested_reasoning_effort or "low"
    if requested_reasoning_effort is None:
        return None
    if (
        _is_hosted_gpt5_model(provider_kind=provider_kind, model=model)
        and structured_mode in {"json_object", "prompt_json"}
    ):
        # For hosted GPT-5 fallback modes, disabling reasoning prioritizes final answer tokens.
        return None
    return requested_reasoning_effort


def _resolve_attempt_max_output_tokens(
    *,
    provider_kind: AIProviderKind,
    model: str,
    stage: AIStageName | None,
    structured_mode: AIStructuredMode,
    requested_max_output_tokens: int | None,
) -> int | None:
    if requested_max_output_tokens is None:
        return None
    if _is_hosted_gpt5_model(provider_kind=provider_kind, model=model) and stage == "triage":
        if structured_mode == "strict_json_schema":
            return max(requested_max_output_tokens, _HOSTED_TRIAGE_STRICT_MIN_OUTPUT_TOKENS)
        if structured_mode == "prompt_json":
            return max(requested_max_output_tokens, _HOSTED_TRIAGE_PROMPT_JSON_MIN_OUTPUT_TOKENS)
    if (
        _is_hosted_gpt5_model(provider_kind=provider_kind, model=model)
        and structured_mode == "prompt_json"
    ):
        return max(requested_max_output_tokens, _HOSTED_PROMPT_JSON_MIN_OUTPUT_TOKENS)
    return requested_max_output_tokens


def _is_hosted_gpt5_model(*, provider_kind: AIProviderKind, model: str) -> bool:
    return provider_kind == "openai" and model.strip().lower().startswith("gpt-5")


def _detect_likely_truncated_json(exc: JSONParseError) -> bool:
    truncated_markers = (
        "unterminated",
        "expecting value",
        "expecting ',' delimiter",
        "expecting property name",
    )
    for failure in exc.diagnostics.failures:
        lower = failure.reason.lower()
        if any(marker in lower for marker in truncated_markers):
            return True
    if not exc.diagnostics.candidates:
        return False
    first_candidate = exc.diagnostics.candidates[0].rstrip()
    if not first_candidate:
        return False
    return not first_candidate.endswith("}")


def _summarize_request_payload(
    *,
    request_payload: dict[str, Any],
    transport: str,
) -> dict[str, Any]:
    summary: dict[str, Any] = {
        "transport": transport,
        "model": request_payload.get("model"),
    }
    if transport == "responses":
        text_cfg = request_payload.get("text")
        format_type: str | None = None
        if isinstance(text_cfg, dict):
            fmt_cfg = text_cfg.get("format")
            if isinstance(fmt_cfg, dict):
                raw = fmt_cfg.get("type")
                if isinstance(raw, str):
                    format_type = raw
        summary["format_type"] = format_type
        reasoning = request_payload.get("reasoning")
        if isinstance(reasoning, dict):
            summary["reasoning_effort"] = reasoning.get("effort")
        summary["temperature"] = request_payload.get("temperature")
        summary["max_output_tokens"] = request_payload.get("max_output_tokens")
        summary["input_messages"] = len(request_payload.get("input", [])) if isinstance(request_payload.get("input"), list) else 0
        return summary

    response_format = request_payload.get("response_format")
    format_type = None
    if isinstance(response_format, dict):
        raw = response_format.get("type")
        if isinstance(raw, str):
            format_type = raw
    summary["format_type"] = format_type
    reasoning = request_payload.get("reasoning")
    if isinstance(reasoning, dict):
        summary["reasoning_effort"] = reasoning.get("effort")
    summary["temperature"] = request_payload.get("temperature")
    summary["max_completion_tokens"] = request_payload.get("max_completion_tokens")
    summary["messages"] = len(request_payload.get("messages", [])) if isinstance(request_payload.get("messages"), list) else 0
    return summary


def _summarize_responses_response(response: Any) -> dict[str, Any]:
    output_text = _extract_text_candidate(_read_response_field(response, "output_text"))
    output_items = _as_response_items(_read_response_field(response, "output"))
    response_status = _response_type_label(_read_response_field(response, "status"))
    raw_incomplete_details = _read_response_field(response, "incomplete_details")
    incomplete_details = _sanitize_for_evidence(raw_incomplete_details)
    incomplete_reason = _response_type_label(_read_response_field(raw_incomplete_details, "reason"))
    stop_reason = _response_type_label(_read_response_field(response, "stop_reason"))
    usage = _sanitize_for_evidence(_read_response_field(response, "usage"))
    item_types: set[str] = set()
    content_types: set[str] = set()
    saw_refusal = False
    output_tokens: int | None = None
    reasoning_tokens: int | None = None

    if isinstance(usage, dict):
        output_tokens_raw = usage.get("output_tokens")
        if isinstance(output_tokens_raw, int):
            output_tokens = output_tokens_raw
        output_tokens_details = usage.get("output_tokens_details")
        if isinstance(output_tokens_details, dict):
            reasoning_tokens_raw = output_tokens_details.get("reasoning_tokens")
            if isinstance(reasoning_tokens_raw, int):
                reasoning_tokens = reasoning_tokens_raw

    for item in output_items:
        item_type = _response_type_label(_read_response_field(item, "type"))
        if item_type is not None:
            item_types.add(item_type)
        refusal_text = _extract_text_candidate(_read_response_field(item, "refusal"))
        if item_type == "refusal" or refusal_text is not None:
            saw_refusal = True
        for content_item in _as_response_items(_read_response_field(item, "content")):
            content_type = _response_type_label(_read_response_field(content_item, "type"))
            if content_type is not None:
                content_types.add(content_type)
            content_refusal = _extract_text_candidate(_read_response_field(content_item, "refusal"))
            if content_type == "refusal" or content_refusal is not None:
                saw_refusal = True

    all_output_tokens_reasoning = (
        output_tokens is not None
        and output_tokens > 0
        and reasoning_tokens is not None
        and reasoning_tokens >= output_tokens
    )
    answer_started = bool(
        output_text is not None
        or "message" in item_types
        or {"output_text", "text", "output_json", "json", "parsed"} & content_types
    )

    return {
        "response_status": response_status,
        "stop_reason": stop_reason,
        "incomplete_details": incomplete_details,
        "incomplete_reason": incomplete_reason,
        "output_text_present": output_text is not None,
        "output_text_preview": _truncate_text(output_text) if output_text is not None else None,
        "output_text_length": len(output_text) if output_text is not None else 0,
        "output_items_count": len(output_items),
        "output_item_types": sorted(item_types),
        "content_item_types": sorted(content_types),
        "answer_started": answer_started,
        "all_output_tokens_reasoning": all_output_tokens_reasoning,
        "saw_refusal": saw_refusal,
        "usage": usage,
    }


def _summarize_chat_response(response: Any) -> dict[str, Any]:
    choices = _as_response_items(_read_response_field(response, "choices"))
    first_choice = choices[0] if choices else None
    message = _read_response_field(first_choice, "message")
    refusal = _extract_text_candidate(_read_response_field(message, "refusal"))
    content = _read_response_field(message, "content")
    content_text = _extract_text_candidate(content)
    content_kind = type(content).__name__ if content is not None else None
    return {
        "choices_count": len(choices),
        "message_present": message is not None,
        "content_kind": content_kind,
        "content_preview": _truncate_text(content_text) if content_text is not None else None,
        "saw_refusal": refusal is not None,
        "refusal_preview": _truncate_text(refusal) if refusal is not None else None,
    }


def _sanitize_for_evidence(value: Any, *, depth: int = 0) -> Any:
    if depth > 7:
        return "<max-depth>"
    if value is None or isinstance(value, (bool, int, float)):
        return value
    if isinstance(value, str):
        return _truncate_text(value)
    if isinstance(value, dict):
        out: dict[str, Any] = {}
        for key, raw in list(value.items())[:_EVIDENCE_LIST_LIMIT]:
            key_str = str(key)
            if _is_sensitive_key(key_str):
                out[key_str] = "<redacted>"
            else:
                out[key_str] = _sanitize_for_evidence(raw, depth=depth + 1)
        return out
    if isinstance(value, (list, tuple)):
        return [
            _sanitize_for_evidence(item, depth=depth + 1)
            for item in list(value)[:_EVIDENCE_LIST_LIMIT]
        ]

    model_dump = getattr(value, "model_dump", None)
    if callable(model_dump):
        try:
            dumped = model_dump(exclude_none=True)
        except TypeError:
            dumped = model_dump()
        return _sanitize_for_evidence(dumped, depth=depth + 1)

    to_dict = getattr(value, "to_dict", None)
    if callable(to_dict):
        return _sanitize_for_evidence(to_dict(), depth=depth + 1)

    return _truncate_text(repr(value))


def _truncate_text(value: str | None, *, limit: int = _EVIDENCE_STRING_LIMIT) -> str:
    if value is None:
        return ""
    if len(value) <= limit:
        return value
    return value[: limit - 3] + "..."


def _is_sensitive_key(key: str) -> bool:
    normalized = key.strip().lower().replace("-", "_")
    if normalized in _SECRET_KEY_EXACT_NAMES:
        return True
    return any(normalized.endswith(suffix) for suffix in _SECRET_KEY_SUFFIXES)


def _serialize_payload_for_failure_capture(value: Any, *, depth: int = 0) -> Any:
    if depth > 20:
        return "<max-depth>"
    if value is None or isinstance(value, (bool, int, float, str)):
        return value
    if isinstance(value, dict):
        return {
            str(key): _serialize_payload_for_failure_capture(item, depth=depth + 1)
            for key, item in value.items()
        }
    if isinstance(value, (list, tuple)):
        return [_serialize_payload_for_failure_capture(item, depth=depth + 1) for item in value]

    model_dump = getattr(value, "model_dump", None)
    if callable(model_dump):
        try:
            dumped = model_dump(exclude_none=True)
        except TypeError:
            dumped = model_dump()
        return _serialize_payload_for_failure_capture(dumped, depth=depth + 1)

    to_dict = getattr(value, "to_dict", None)
    if callable(to_dict):
        return _serialize_payload_for_failure_capture(to_dict(), depth=depth + 1)

    return repr(value)


def _safe_json_clone(value: Any) -> Any:
    return json.loads(json.dumps(value))


def _is_mode_recoverable_error(message: str) -> bool:
    lower = message.lower()
    return any(marker in lower for marker in _MODE_RECOVERABLE_ERROR_MARKERS) or "ai parse error" in lower
