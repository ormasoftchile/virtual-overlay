# Contracts: Virtual Overlay

**Status**: N/A — Not Applicable

This is a native Windows desktop application with no network APIs.

## Why No Contracts

- **No REST/GraphQL endpoints** — Application runs locally only
- **No client-server communication** — Single process
- **No inter-service contracts** — Monolithic architecture

## Internal Interfaces

For internal component interfaces, see:

- [data-model.md](../data-model.md) — Configuration schema and state structures
- [research.md](../research.md) — Windows API usage patterns

## Future Consideration

If the application ever exposes:
- A local HTTP API for automation
- IPC for external integrations
- Plugin interface

Then contracts would be defined here using OpenAPI/JSON Schema.
