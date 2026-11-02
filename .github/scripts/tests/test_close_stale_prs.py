import importlib.util
import io
import unittest
from contextlib import redirect_stdout
from datetime import datetime, timezone
from pathlib import Path


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
        self.on_second_detail_read = None
        self.fail_close = False

    def get(self, path):
        if "/pulls?" in path:
            page = int(path.rsplit("page=", 1)[1])
            self.list_pages.append(page)
            return self.pulls[(page - 1) * 100 : page * 100]
        number = int(path.split("/")[5])
        if path.endswith(f"/pulls/{number}"):
            self.detail_reads[number] = self.detail_reads.get(number, 0) + 1
            if self.detail_reads[number] == 2 and self.on_second_detail_read:
                self.on_second_detail_read(self.details[number])
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

        api.on_second_detail_read = lambda detail: detail.update({"updated_at": NEW})
        output = self.run_script(api, "--apply", "--confirm-repo", "meta/fboss")
        self.assertIn("changed since the scan; skipping", output)
        self.assertEqual([], api.closed)
        self.assertEqual([], api.comments)

    def test_apply_limit_and_pagination(self):
        recent = [pull(number, NEW) for number in range(3, 102)]
        api = FakeGitHub(recent + [pull(1), pull(2)])
        output = self.run_script(
            api, "--apply", "--confirm-repo", "meta/fboss", "--max-close", "1"
        )
        self.assertEqual([1, 2], api.list_pages)
        self.assertEqual([1], api.closed)
        self.assertEqual(1, len(api.comments))
        self.assertEqual(1, api.comments[0][0])
        self.assertIn("rebase", api.comments[0][1])
        self.assertIn("reopen", api.comments[0][1])
        self.assertIn("2 candidate(s)", output)
        self.assertIn("Closed 1 PR(s)", output)

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
