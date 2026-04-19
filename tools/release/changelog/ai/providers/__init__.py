from tools.release.changelog.ai.config import AIProviderConfig, DEFAULT_OPENAI_COMPATIBLE_BASE_URL
from tools.release.changelog.ai.providers.base import StructuredJSONProvider
from tools.release.changelog.ai.providers.openai import OpenAIProvider
from tools.release.changelog.ai.providers.openai_compatible import OpenAICompatibleProvider


def build_structured_json_provider(config: AIProviderConfig) -> StructuredJSONProvider:
    if config.kind == "openai":
        if config.base_url:
            raise ValueError("`--base-url` is only valid when `--provider openai-compatible` is used.")
        return OpenAIProvider(
            model=config.model,
            api_key=config.api_key,
            api_key_env_var=config.api_key_env_var,
            timeout_seconds=config.timeout_seconds,
        )

    if config.kind == "openai-compatible":
        return OpenAICompatibleProvider(
            model=config.model,
            base_url=config.base_url or DEFAULT_OPENAI_COMPATIBLE_BASE_URL,
            api_key=config.api_key,
            api_key_env_var=config.api_key_env_var,
            timeout_seconds=config.timeout_seconds,
        )

    raise ValueError(f"Unsupported AI provider kind: {config.kind}")


__all__ = [
    "StructuredJSONProvider",
    "OpenAIProvider",
    "OpenAICompatibleProvider",
    "build_structured_json_provider",
]
