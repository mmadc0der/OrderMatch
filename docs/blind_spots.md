# V1 Architecture Gaps And Decisions

This is a temporary working document. Keep it only until the implementation and the durable docs have absorbed the decisions here, then delete it.

## Purpose

This file tracks the gap between the intended V1 system and the current implementation. It is meant to shape the next jobs, not to act as permanent product documentation.

Enterprise-grade here means small surface area, explicit ownership boundaries, deterministic behavior, bounded queues, observable failure modes, and no claims that exceed the code.

## Current State

The repository already has the intended module split and several domain types, and the matching core now performs deterministic active-liquidity matching.

- The matching core book logic is working, but the surrounding engine/reporting layers still do not consume its facts end-to-end.
- The engine layer has shapes for events, reports, and read views, but not the full data flow.
- The HTTP layer and server entry points are placeholders, not a complete API implementation.
- Uid registration, ownership tracking, queue backpressure, and broader integration coverage are still missing.

That means the architecture docs describe the target contract, while this file should be read as the current-state gap analysis.

## V1 Quality Bar

Minimal functionality still needs to be resilient and explicit.

- Deterministic matching and stable result codes.
- Rejection paths that do not mutate book state.
- Bounded queues with a clear overload outcome.
- Compact domain types at the core boundary.
- No security language unless there is actual secret-bearing authentication.
- Tests that prove matching behavior, not just type layout.

## Stable Boundaries

These decisions should remain consistent across the docs and implementation.

- HTTP code handles routing, shape validation, and transport errors.
- Decimal parsing and instrument range validation happen before the core.
- The matching core never parses or formats JSON or decimal strings.
- `RequestId` is transport correlation only. It is not idempotency.
- `OrderId` is the stable engine identity.
- `uid` lives outside the matching core and is only for attribution and ownership filtering.
- The lightweight bearer hash is a payload-binding aid, not authentication.

## Resilience Rules

The system should fail cleanly rather than cleverly.

- Reject overload before an event enters the engine queue.
- Do not allow partial engine mutation on rejected input.
- Treat outbound execution reports as owning messages across async boundaries.
- Let `GET /book` return a bounded-stale snapshot if that is the chosen mode, but include `sequence` so freshness is visible.
- Keep V1 recovery promises in-memory only unless persistence is explicitly added later.
- Expose operational health from the transport layer without mutating engine state.

## Implementation Jobs

These are the concrete gaps that need work before the architecture claims become real.

1. Wire the engine/reporting layer to the core operation facts and fills.
2. Populate execution reports with real fill and residual data.
3. Implement codec and domain mapping for REST requests and responses.
4. Wire the event bus and backpressure behavior into the engine runner.
5. Implement book view baking with explicit depth semantics.
6. Add uid registration and ownership checks outside the matching core.
7. Add tests for invariants, API contracts, and report shape.

## Deferred Work

These items stay intentionally out of V1 unless the scope changes.

- Persistence and recovery.
- Real authentication and secret handling.
- Replay durability across restarts.
- Multi-instrument routing.
- Balances, custody, margin, and settlement.
- Full account and permissions models.

## Exit Criteria

This document can be removed once the behavior above is implemented, the permanent docs no longer overstate the system, and the tests prove the core contracts.

