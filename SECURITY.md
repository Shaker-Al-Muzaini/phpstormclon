# Security Policy

## Supported Versions

| Version | Supported |
|---------|----------:|
| 0.x (dev) | Yes |

## Reporting a Vulnerability

Please **do not** open a public GitHub issue for security vulnerabilities.

Instead, email: security@forge-ide.dev (or file a GitHub private security advisory).

Include:
- Description of the vulnerability
- Steps to reproduce
- Potential impact
- Any suggested mitigations

We aim to respond within 72 hours and issue a patch within 14 days for critical issues.

## Scope

- Local privilege escalation through plugin loading
- Arbitrary code execution via malicious project files
- Credential exposure in database tool or Git credential helper
- Remote code execution via Xdebug/DBGp path mapping

Out of scope: denial-of-service from malformed PHP files (parser crashes should be filed as regular bugs).
