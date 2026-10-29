# Security Policy

## Security maintainers

Dawn's security maintainers are [@chnsw](https://github.com/chnsw), [@TempAccountNull](https://github.com/TempAccountNull), [@flummoxed](https://github.com/di-donna), [@isinternets](https://github.com/isinternets) and [@ocucor](https://github.com/ocucor/).

[@chnsw](https://github.com/chnsw), [@flummoxed](https://github.com/di-donna), [@TempAccountNull](https://github.com/TempAccountNull), [@isinternets](https://github.com/isinternets) and [@ocucor](https://github.com/ocucor/)
handle Dawn's client security reviews, vulnerability triage, and security fixes,
coordinating with other project maintainers where necessary.

## Supported versions

Security fixes target the latest stable release and the `production` branch.
Users of older releases may need to upgrade to receive a fix.

## Reporting a vulnerability

Please report suspected security vulnerabilities privately through GitHub
when private vulnerability reporting is available:

1. Open this repository's [Security tab](https://github.com/isinternets/Dawn/security).
2. Select **Report a vulnerability**.
3. Describe the issue using the private reporting form.

If **Report a vulnerability** is not available, contact
[@chnsw](https://github.com/chnsw), or [@flummoxed](https://github.com/di-donna) privately using the Discord contact listed
on their GitHub profile. A repository administrator must enable GitHub's
private vulnerability reporting before that reporting form can be used.

Please avoid posting details of an unfixed vulnerability in public issues,
pull requests, or discussions.

Include, where possible:

- The affected Dawn version or commit.
- A description of the issue and its security impact.
- Steps to reproduce it in a local test environment.
- Relevant logs or a minimal proof of concept.
- Any suggested mitigation or fix.

Remove credentials, personal information, and unrelated private data from
your report.

## Scope

We welcome reports affecting Dawn's client code, integrations, dependencies,
and installation or update tools.

Examples include unsafe handling of untrusted input, memory safety
vulnerabilities, unintended code execution, exposure of sensitive information,
and unsafe file operations.

Ordinary gameplay bugs without a security impact should be reported through
the public issue tracker.

## Handling and disclosure

We will review reports, investigate their impact, and coordinate fixes and
disclosure with the reporter.

Please coordinate public disclosure with the maintainers so affected users
can receive a fix or mitigation.

Reporters may request public credit or remain anonymous.

## Research environment

Perform testing only on installations and systems you own or have permission
to test. Dawn's reporting process does not authorize testing Bungie's services
or other people's systems.
