# Contributing to FBOSS

We want to make contributing to this project as easy and transparent as
possible.

## Pull Requests
We actively welcome your pull requests.
1. Fork the repo and create your branch from `master`.
2. If you've added code that should be tested, add tests
3. If you've changed APIs, update the documentation.
4. Ensure the test suite passes.
5. Make sure your code lints.
6. If you haven't already, complete the Contributor License Agreement ("CLA").

## Contributor License Agreement ("CLA")
In order to accept your pull request, we need you to submit a CLA. You only need
to do this once to work on any of Facebook's open source projects.

Complete your CLA here: <https://code.facebook.com/cla>

## Issues
We use GitHub issues to track public bugs. Please ensure your description is
clear and has sufficient instructions to be able to reproduce the issue.

Facebook has a [bounty program](https://www.facebook.com/whitehat/) for the safe
disclosure of security bugs. In those cases, please go through the process
outlined on that page and do not file a public issue.

## Coding Style
Please try to follow the existing coding style within the FBOSS files.
For C++ code, our style is
* 2 spaces for indentation rather than tabs
* 80 character line length
* Cuddled curly braces

## Copyright Headers

Every new C or C++ source and header file must use the canonical Meta notice
without a year, regardless of whether the contribution originates from a Meta
employee or an external GitHub contributor:

```cpp
// Copyright (c) Meta Platforms, Inc. and affiliates.
```

External contributions accepted under the FBOSS Contributor License Agreement
follow the same project-level header convention. Do not add a personal or
employer-specific copyright line unless an FBOSS maintainer or Meta Open Source
Legal explicitly directs you to do so.

Preserve the existing copyright and license notices in generated, vendored, or
third-party code. Internal-only notices containing `Confidential and
proprietary` must never be added to the public repository. The pre-commit
checks enforce this policy for newly added C and C++ files.

## License
By contributing to FBOSS, you agree that your contributions will be licensed
under its BSD license.
