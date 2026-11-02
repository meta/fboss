#!/usr/bin/env python3
"""Preview or close inactive meta/fboss pull requests.

Run from the FBOSS repository with GITHUB_TOKEN set:

    python3 .github/scripts/close-stale-prs.py
    python3 .github/scripts/close-stale-prs.py --apply --confirm-repo meta/fboss

By default this only prints candidates. A token with pull requests and issues
write permission is needed for --apply.
"""

import argparse
import json
import os
import sys
from datetime import datetime, timedelta, timezone
from urllib.error import HTTPError
from urllib.request import Request, urlopen


REPO = "meta/fboss"
API = "https://api.github.com"


def close_comment(days):
    return (
        f"This pull request has been closed because it has been inactive for over {days} days. "
        "If these changes are still needed, please rebase onto the current main branch "
        "and reopen the pull request."
    )


class GitHubAPI:
    def __init__(self, token):
        self.token = token

    def request(self, method, path, body=None):
        data = json.dumps(body).encode() if body is not None else None
        request = Request(
            API + path,
            data=data,
            method=method,
            headers={
                "Accept": "application/vnd.github+json",
                "Authorization": f"Bearer {self.token}",
                "Content-Type": "application/json",
                "X-GitHub-Api-Version": "2022-11-28",
            },
        )
        try:
            with urlopen(request, timeout=30) as response:
                return json.load(response)
        except HTTPError as error:
            raise RuntimeError(
                f"GitHub API {method} {path} returned {error.code}"
            ) from error

    def get(self, path):
        return self.request("GET", path)

    def close(self, number):
        result = self.request(
            "PATCH", f"/repos/{REPO}/pulls/{number}", {"state": "closed"}
        )
        if result["state"] != "closed":
            raise RuntimeError(f"GitHub did not close PR #{number}")

    def comment(self, number, body):
        self.request("POST", f"/repos/{REPO}/issues/{number}/comments", {"body": body})


def timestamp(value):
    return datetime.fromisoformat(value.replace("Z", "+00:00"))


def pages(api, path):
    page = 1
    while True:
        batch = api.get(f"{path}{'&' if '?' in path else '?'}per_page=100&page={page}")
        yield from batch
        if len(batch) < 100:
            return
        page += 1


def is_stale(api, number, cutoff, include_drafts, exempt_labels):
    pull = api.get(f"/repos/{REPO}/pulls/{number}")
    if pull["state"] != "open" or (pull["draft"] and not include_drafts):
        return False
    if any(label["name"].lower() in exempt_labels for label in pull["labels"]):
        return False
    if max(timestamp(pull[field]) for field in ("created_at", "updated_at")) >= cutoff:
        return False

    for path in (
        f"/repos/{REPO}/issues/{number}/comments?sort=updated&direction=desc",
        f"/repos/{REPO}/pulls/{number}/comments?sort=updated&direction=desc",
    ):
        comments = api.get(f"{path}&per_page=1")
        if comments and timestamp(comments[0]["updated_at"]) >= cutoff:
            return False

    reviews = pages(api, f"/repos/{REPO}/pulls/{number}/reviews")
    return all(
        timestamp(review["submitted_at"]) < cutoff
        for review in reviews
        if review.get("submitted_at")
    )


def candidates(api, cutoff, include_drafts=False, exempt_labels=frozenset()):
    path = f"/repos/{REPO}/pulls?state=open&sort=updated&direction=asc"
    for pull in pages(api, path):
        if timestamp(pull["updated_at"]) >= cutoff:
            continue
        if is_stale(api, pull["number"], cutoff, include_drafts, exempt_labels):
            yield pull


def main(argv=None, api=None, now=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--days", type=int, default=30, help="Inactivity cutoff in days (default: 30)"
    )
    parser.add_argument("--include-drafts", action="store_true")
    parser.add_argument(
        "--exempt-label",
        action="append",
        default=["do-not-close"],
        help="Never close PRs with this label (default: do-not-close)",
    )
    parser.add_argument(
        "--apply", action="store_true", help="Actually close matching PRs"
    )
    parser.add_argument(
        "--confirm-repo", help="Required with --apply; must be meta/fboss"
    )
    parser.add_argument(
        "--max-close",
        type=int,
        default=10,
        help="Maximum PRs to close per run (default: 10)",
    )
    args = parser.parse_args(argv)
    if args.days < 1 or args.max_close < 1:
        parser.error("--days and --max-close must be positive")
    if args.apply and args.confirm_repo != REPO:
        parser.error(f"--apply requires --confirm-repo {REPO}")

    if api is None:
        token = os.environ.get("GITHUB_TOKEN")
        if not token:
            parser.error("set GITHUB_TOKEN to a token with pull request read access")
        api = GitHubAPI(token)

    cutoff = (now or datetime.now(timezone.utc)) - timedelta(days=args.days)
    exempt_labels = {label.lower() for label in args.exempt_label}
    matches = list(candidates(api, cutoff, args.include_drafts, exempt_labels))
    for pull in matches:
        print(f"#{pull['number']}  updated {pull['updated_at']}  {pull['title']}")
    print(f"{len(matches)} candidate(s); cutoff {cutoff.isoformat()}")
    if not args.apply:
        print(
            "Dry run: nothing closed. Use --apply --confirm-repo meta/fboss to close PRs."
        )
        return 0

    closed = 0
    for pull in matches[: args.max_close]:
        number = pull["number"]
        if not is_stale(api, number, cutoff, args.include_drafts, exempt_labels):
            print(f"#{number} changed since the scan; skipping")
            continue
        api.close(number)
        api.comment(number, close_comment(args.days))
        print(f"Closed and commented on #{number}")
        closed += 1
    print(f"Closed {closed} PR(s); limit {args.max_close} per run")
    return 0


if __name__ == "__main__":
    sys.exit(main())
