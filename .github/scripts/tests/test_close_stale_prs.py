import importlib.util
import io
import json
import unittest
from contextlib import redirect_stdout
from datetime import datetime, timezone
from pathlib import Path
from unittest.mock import patch
from urllib.error import HTTPError


SCRIPT = Path(__file__).resolve().parents[1] / "close-stale-prs.py"
spec = importlib.util.spec_from_file_location("close_stale_prs", SCRIPT)
stale_prs = importlib.util.module_from_spec(spec)
spec.loader.exec_module(stale_prs)

NOW = datetime(2026, 11, 2, tzinfo=timezone.utc)
OLD = "2026-09-01T00:00:00Z"
NEW = "2026-10-30T00:00:00Z"


def pull(number, updated=OLD, draft=False, labels=()):
    return {
        "number": number,
        "title": f"PR {number}",
        "created_at": OLD,
        "updated_at": updated,
        "draft": draft,
        "state": "open",
        "labels": [{"name": label} for label in labels],
    }


class FakeGitHub:
    def __init__(self, pulls):
        self.pulls = pulls
        self.details = {entry["number"]: entry.copy() for entry in pulls}
        self.issue_comments = {}
        self.review_comments = {}
        self.reviews = {}
        self.closed = []
        self.comments = []
        self.list_pages = []
        self.detail_reads = {}
        self.on_detail_read = None
        self.fail_close = False
        self.fail_comment_for = set()

    def get(self, path):
        if "/pulls?" in path:
            page = int(path.rsplit("page=", 1)[1])
            self.list_pages.append(page)
            return self.pulls[(page - 1) * 100 : page * 100]
        number = int(path.split("/")[5])
        if path.endswith(f"/pulls/{number}"):
            self.detail_reads[number] = self.detail_reads.get(number, 0) + 1
            if self.on_detail_read:
                self.on_detail_read(self.details[number])
            return self.details[number]
        if "/issues/" in path:
            return self.issue_comments.get(number, [])
        if "/reviews?" in path:
            page = int(path.rsplit("page=", 1)[1])
            return self.reviews.get(number, [])[(page - 1) * 100 : page * 100]
        return self.review_comments.get(number, [])

    def close(self, number):
        if self.fail_close:
            raise RuntimeError("GitHub did not close the PR")
        self.closed.append(number)
        return {"state": "closed"}

    def comment(self, number, body):
        if number in self.fail_comment_for:
            raise RuntimeError("GitHub comments endpoint returned 500")
        self.comments.append((number, body))


class CloseStalePRsTest(unittest.TestCase):
    def run_script(self, api, *args):
        output = io.StringIO()
        with redirect_stdout(output):
            result = stale_prs.main(list(args), api=api, now=NOW)
        self.assertEqual(0, result)
        return output.getvalue()

    def test_dry_run_lists_only_inactive_nondraft_prs(self):
        api = FakeGitHub([pull(1), pull(2, NEW), pull(3, draft=True)])
        output = self.run_script(api)
        self.assertIn("#1  updated", output)
        self.assertIn("1 candidate(s)", output)
        self.assertNotIn("#2  updated", output)
        self.assertNotIn("#3  updated", output)
        self.assertEqual([], api.closed)
        self.assertEqual([], api.comments)

    def test_comments_reviews_and_exempt_labels_are_activity_or_protection(self):
        api = FakeGitHub([pull(1), pull(2), pull(3), pull(4, labels=["DO-NOT-CLOSE"])])
        api.issue_comments[1] = [{"updated_at": NEW}]
        api.review_comments[2] = [{"updated_at": NEW}]
        api.reviews[3] = [{"submitted_at": NEW}]
        output = self.run_script(api, "--apply", "--confirm-repo", "meta/fboss")
        self.assertIn("0 candidate(s)", output)
        self.assertEqual([], api.closed)
        self.assertEqual([], api.comments)

    def test_apply_requires_confirmation_and_rechecks_before_close(self):
        api = FakeGitHub([pull(1), pull(2)])
        with self.assertRaises(SystemExit):
            self.run_script(api, "--apply")
        self.assertEqual([], api.closed)

        api.on_detail_read = lambda detail: detail.update({"updated_at": NEW})
        output = self.run_script(api, "--apply", "--confirm-repo", "meta/fboss")
        self.assertIn("changed since the scan; skipping", output)
        self.assertEqual([], api.closed)
        self.assertEqual([], api.comments)

    def test_apply_closes_all_candidates_across_pages(self):
        stale = [pull(number) for number in range(1, 13)]
        recent = [pull(number, NEW) for number in range(13, 112)]
        api = FakeGitHub(stale + recent)
        output = self.run_script(api, "--apply", "--confirm-repo", "meta/fboss")
        self.assertEqual([1, 2], api.list_pages)
        self.assertEqual(dict.fromkeys(range(1, 13), 1), api.detail_reads)
        self.assertEqual(list(range(1, 13)), api.closed)
        self.assertEqual(12, len(api.comments))
        self.assertEqual(1, api.comments[0][0])
        self.assertIn("rebase", api.comments[0][1])
        self.assertIn("reopen", api.comments[0][1])
        self.assertIn("12 candidate(s)", output)
        self.assertIn("Closed 12 PR(s)", output)

    def test_recent_review_on_second_page_prevents_closing(self):
        api = FakeGitHub([pull(1)])
        api.reviews[1] = [{"submitted_at": OLD} for _ in range(100)] + [
            {"submitted_at": NEW}
        ]
        output = self.run_script(api, "--apply", "--confirm-repo", "meta/fboss")
        self.assertIn("0 candidate(s)", output)
        self.assertEqual([], api.closed)

    def test_failed_close_does_not_leave_comment(self):
        api = FakeGitHub([pull(1)])
        api.fail_close = True
        with self.assertRaisesRegex(RuntimeError, "did not close"):
            with redirect_stdout(io.StringIO()):
                stale_prs.main(
                    ["--apply", "--confirm-repo", "meta/fboss"], api=api, now=NOW
                )
        self.assertEqual([], api.comments)

    def test_failed_comment_reports_failure_and_continues_closing(self):
        api = FakeGitHub([pull(1), pull(2)])
        api.fail_comment_for.add(1)
        stdout = io.StringIO()
        stderr = io.StringIO()
        with redirect_stdout(stdout), patch("sys.stderr", stderr):
            result = stale_prs.main(
                ["--apply", "--confirm-repo", "meta/fboss"], api=api, now=NOW
            )
        self.assertEqual(1, result)
        self.assertEqual([1, 2], api.closed)
        self.assertEqual([2], [number for number, _ in api.comments])
        self.assertIn("Closed 2 PR(s)", stdout.getvalue())
        self.assertIn("Comments failed on PRs: [1]", stderr.getvalue())

    def test_rate_limit_retries_then_succeeds(self):
        error = HTTPError(
            "https://api.github.com/repos/meta/fboss/pulls/1",
            429,
            "Too Many Requests",
            {"Retry-After": "2"},
            io.BytesIO(b""),
        )
        with (
            patch.object(
                stale_prs, "urlopen", side_effect=[error, io.BytesIO(b"[]")]
            ) as urlopen,
            patch.object(stale_prs.time, "sleep") as sleep,
        ):
            result = stale_prs.GitHubAPI("token").get("/repos/meta/fboss/pulls/1")
        self.assertEqual([], result)
        self.assertEqual(2, urlopen.call_count)
        sleep.assert_called_once_with(2)

    def test_permission_denied_does_not_retry(self):
        error = HTTPError(
            "https://api.github.com/repos/meta/fboss/pulls/1",
            403,
            "Forbidden",
            {},
            io.BytesIO(b'{"message": "Resource not accessible"}'),
        )
        with (
            patch.object(stale_prs, "urlopen", side_effect=error) as urlopen,
            patch.object(stale_prs.time, "sleep") as sleep,
        ):
            with self.assertRaisesRegex(RuntimeError, "returned 403"):
                stale_prs.GitHubAPI("token").get("/repos/meta/fboss/pulls/1")
        self.assertEqual(1, urlopen.call_count)
        sleep.assert_not_called()

    def test_secondary_rate_limit_retries(self):
        error = HTTPError(
            "https://api.github.com/repos/meta/fboss/pulls/1",
            403,
            "Forbidden",
            {},
            io.BytesIO(b'{"message": "You have exceeded a secondary rate limit"}'),
        )
        with (
            patch.object(stale_prs, "urlopen", side_effect=[error, io.BytesIO(b"[]")]),
            patch.object(stale_prs.time, "sleep") as sleep,
        ):
            result = stale_prs.GitHubAPI("token").get("/repos/meta/fboss/pulls/1")
        self.assertEqual([], result)
        sleep.assert_called_once_with(60)

    def test_transient_comment_failure_retries_after_checking_for_duplicate(self):
        error = HTTPError(
            "https://api.github.com/repos/meta/fboss/issues/1/comments",
            500,
            "Server Error",
            {},
            io.BytesIO(b""),
        )
        with (
            patch.object(
                stale_prs,
                "urlopen",
                side_effect=[error, io.BytesIO(b"[]"), io.BytesIO(b"{}")],
            ) as urlopen,
            patch.object(stale_prs.time, "sleep") as sleep,
        ):
            stale_prs.GitHubAPI("token").comment(1, "Closing inactive PR")
        self.assertEqual(3, urlopen.call_count)
        self.assertEqual(
            ["POST", "GET", "POST"],
            [call.args[0].get_method() for call in urlopen.call_args_list],
        )
        sleep.assert_called_once_with(1)

    def test_transient_comment_failure_does_not_post_duplicate(self):
        error = HTTPError(
            "https://api.github.com/repos/meta/fboss/issues/1/comments",
            500,
            "Server Error",
            {},
            io.BytesIO(b""),
        )
        existing = [
            {
                "body": "Closing inactive PR",
                "created_at": datetime.now(timezone.utc).isoformat(),
            }
        ]
        with (
            patch.object(
                stale_prs,
                "urlopen",
                side_effect=[error, io.BytesIO(json.dumps(existing).encode())],
            ) as urlopen,
            patch.object(stale_prs.time, "sleep") as sleep,
        ):
            stale_prs.GitHubAPI("token").comment(1, "Closing inactive PR")
        self.assertEqual(2, urlopen.call_count)
        sleep.assert_not_called()
