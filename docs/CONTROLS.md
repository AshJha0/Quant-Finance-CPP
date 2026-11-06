# Trading controls against RTS 6 and SEC Rule 15c3-5

**This is not a compliance certification, and not legal advice.** It is an engineering
map: which requirements have code behind them in this library, and which do not. Both
rules bind firms, not libraries. Most of what they ask for is governance, people,
testing and records, which a library cannot supply. No regulator, lawyer or venue has
reviewed this document or this code, and no venue has certified it.

## Sources and how far they were checked

Read on 2026-11-06, in paraphrase. Quote nothing here as the rule; go to the source.

| Rule | Source read | Limits of the check |
|---|---|---|
| SEC Rule 15c3-5, 17 CFR 240.15c3-5 | [Cornell LII](https://www.law.cornell.edu/cfr/text/17/240.15c3-5) | Whole section read. SEC guidance and FAQs not read. |
| RTS 6, Delegated Regulation (EU) 2017/589 | [legislation.gov.uk](https://www.legislation.gov.uk/eur/2017/589/contents) | The UK retained copy, which that site marks as revoked by 2023 c. 29. EUR-Lex returned nothing readable, so the EU text and its current status are **unverified**. Article numbers and titles come from the contents page. Paragraphs were read only for Articles 12, 15, 16, 17, 18 and 28; every other row rests on the title alone. |

MiFID II Article 17 itself and any FCA successor rules were not read.

Status words: **Implemented** means code and a test exist for the mechanism. It does not
mean the requirement is met in full. **Partial** means a piece exists and the gap is
named. **Absent** means nothing here does it. **Out of scope** means it is firm
governance that code cannot hold.

Paths are under `src/quantfinlib/`. "Gate" is `trading/hft_risk_gate`, "gateway" is
`trading/hft_order_gateway`, "bridge" is `trading/fix_venue_bridge`.

## RTS 6

| Article | Requirement in brief | Status | What exists, and the gap |
|---|---|---|---|
| 15(1)(a) | Price collars block or cancel orders | Implemented | Gate collar against a reference price. A missing reference rejects unless `disableReferenceCheck()` was called on purpose. Per order only, not over a period. |
| 15(1)(b) | Maximum order value | Implemented | `maxOrderNotional`; a fresh gate has a limit of zero. |
| 15(1)(c) | Maximum order volume | Implemented | `maxOrderQuantity`. |
| 15(1)(d) | Maximum message limits | Partial | `trading/order_throttle.hpp` is a token bucket, but **the gateway does not call it**. The caller has to. |
| 15(5) | Block orders from unpermitted traders, and orders that endanger the firm's risk thresholds | Partial | Position cap counts working orders, and `GlobalRiskAggregator` trips on firm gross notional. There is no notion of a trader or a permission. |
| 12(1) | Withdraw all or some pending orders at once | Implemented | Latched per-source kill, `cancelAll`, reduce-only while killed. Covers one gateway, so one venue session. "Some" means single-order `cancel`; no cancel by desk or symbol. Cancels need a live session: during an outage they wait for reconnect, and the venue's own cancel-on-disconnect, if any, is a matter of contract with it. |
| 12(2) | Covers orders of individual traders, desks, clients | Absent | Orders carry no such attribute. |
| 12(3) | Identify the algorithm and the trader, desk or client behind each order | Absent | ClOrdID maps to an order, nothing more. |
| 16(1), (5) | Continuous monitoring; real-time alerts within five seconds | Partial | Kill masks and counters can be read at any time, and the kill itself is immediate. Nothing raises an alert, watches for disorderly trading, or sees venue circuit breakers. |
| 16(2)-(4) | Who monitors, how they respond, how they are reached | Out of scope | |
| 17(1) | Post-trade controls run continuously and act | Partial | Overfill, unknown or mismatched fill, CumQty gap and bust all latch `KILL_GATEWAY`. |
| 17(2) | Market and credit risk from actual exposure | Partial | Live positions and gross notional. No credit model. |
| 17(3) | Complete records, reconciled with venues; exposure in real time | Partial | Exposure is live, and each fill's CumQty is checked against our count. No end-of-day or drop-copy reconciliation, and no order status request after a reconnect. |
| 17(4) | Derivative position limits | Partial | One cap per gate in instrument units. No long/short/strategy split. |
| 18(2) | Protections for data, including access control | Partial | Logon gate, CompID checks and an acceptor `authenticateLogon` hook. **No TLS**: see below. |
| 18(1), (3)-(5) | Technology plan, breach reporting, annual penetration tests, tracing critical access | Out of scope | Fuzz targets exist under `fuzz/`; they do not stand in for a penetration test. |
| 28 | High-frequency traders record each order per Annex II, kept five years | Absent | No order audit log. The FIX store holds outbound messages for resend; it is not a record of this kind. |
| 13 | Automated surveillance for manipulation (title only) | Absent | Self-trade prevention is a pre-trade rule, not surveillance. |
| 14 | Business continuity (title only) | Partial | Dead-man kill, durable session store, resend recovery. The rest is out of scope. Gateway state does not survive a restart. |
| 5-8, 10 | Testing, conformance, deployment, stress (titles only) | Partial | The repository has unit, loopback and sanitizer tests. No venue conformance test has been run, and there is no controlled deployment tooling. |
| 1-4, 9, 11 | Governance, compliance, staffing, outsourcing, self-assessment, change management (titles only) | Out of scope | |
| 19-27 | DEA and clearing (titles only) | Absent | Not attempted. |

## SEC Rule 15c3-5

| Paragraph | Requirement in brief | Status | What exists, and the gap |
|---|---|---|---|
| (b) | Documented risk controls and supervisory procedures | Out of scope | This file describes code, not a firm's procedures. |
| (c)(1)(i) | Reject orders breaching preset credit or capital thresholds | Partial | Per-symbol position cap with working orders counted, and order notional, are pre-trade. The firm gross cap in `GlobalRiskAggregator` polls, so it trips after the breach, not before the order. No per-customer limits. |
| (c)(1)(ii) | Reject erroneous orders on price or size, order by order or over a short period; reject duplicates | Partial | Price and size order by order: yes. Over a short period: no. **No duplicate-order check.** |
| (c)(2)(i) | Block orders until pre-entry regulatory checks pass | Absent | No such checks, for example short sale or locate, exist. Only a per-symbol halt flag. |
| (c)(2)(ii) | Block restricted persons and securities | Partial | `halt(symbol)` blocks a security. No persons. |
| (c)(2)(iii) | Limit system access to authorized persons | Partial | Acceptor logon hook only. |
| (c)(2)(iv) | Immediate execution reports to surveillance staff | Absent | Reports reach the gateway; nothing feeds surveillance. |
| (d) | Controls under the firm's direct and sole control | Out of scope | A deployment question. |
| (e) | Regular review, annual CEO certification | Out of scope | |

## Transport security

TLS is deliberately out. The library has zero dependencies, and a TLS stack is one this
project will not write itself. FIX here runs in clear text, with Username(553) and
Password(554) readable on the wire. A deployment needs a private line, or a stunnel-style terminator
in front. This is a gap against RTS 6 Article 18(2), not a solved item.

## Behaviour to know before relying on the kill

- A kill never clears itself. Each source clears only its own bit, and a reconnect does
  not reset `KILL_VENUE_LINK`.
- After a lost link, reservations are held: the position cap keeps counting orders the
  venue may still hold.
- An order the venue never received, with no reply to our cancel, stays reserved until
  restart. There is no order status query yet, so an operator has to resolve it.
- ExecID history and the order table live in memory. After a restart, replayed fills
  are not recognised, and old ClOrdIDs may read as unknown (which fails closed).
