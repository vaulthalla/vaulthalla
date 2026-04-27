from tools.release.tests.helpers.cli_harness import CliHarness


class ChangelogCliHarness(CliHarness):
    def mock_build_ai_payload(self, payload):
        return self.patch(
            "tools.release.cli.build_ai_payload",
            return_value=payload,
        )

    def mock_semantic_payload(self, payload):
        return self.patch(
            "tools.release.cli.build_semantic_ai_payload",
            return_value=payload,
        )

    def mock_provider(self, provider):
        return self.patch(
            "tools.release.cli.build_ai_provider_from_args",
            return_value=provider,
        )

    def mock_draft(self, markdown="# AI Draft\n"):
        self.patch(
            "tools.release.cli.generate_draft_from_payload",
            return_value=object(),
        )
        return self.patch(
            "tools.release.cli.render_draft_markdown",
            return_value=markdown,
        )

    def mock_emergency_triage(self, result, json_str):
        self.patch(
            "tools.release.cli.run_emergency_triage_stage",
            return_value=result,
        )
        return self.patch(
            "tools.release.cli.render_emergency_triage_result_json",
            return_value=json_str,
        )

    def mock_triage(self, result=object()):
        return self.patch(
            "tools.release.cli.run_triage_stage",
            return_value=result,
        )
