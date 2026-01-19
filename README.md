# cram

A tiny, dependency-free CLI flashcard cramming tool. It shows a single prompt at a time, advances on keypress, and never tracks progress.

## What it is
- A terminal program that shows one prompt at the top-left.
- Prompts within a group are shuffled and shown without repeats until exhausted.
- A dumb, line-oriented parser for a simple text format.
- Fixed-size, compile-time bounded storage (no dynamic allocation after init).

## What it isn't
- A spaced-repetition system.
- A progress tracker.
- A JSON/YAML/TOML-based tool.

## File format
Plain UTF-8 text.

- Comments start with `#` (whole-line).
- Blank lines are ignored.
- A group header line looks exactly like:
  `[Group name | seconds]`
  - `Group name` can be any text not containing `]`.
  - `seconds` is a positive integer (1..86400).
- The `seconds` field sets how long each group runs before switching.
- After a header, each non-blank non-comment line is a prompt until the next header.
- A group must have at least 1 item.
- If an item appears before any header, it's an error.
- If a header is malformed, it's an error.

Example:
```
# Countries (capitals)
[Europe | 60]
Capital: France
Capital: Germany
Capital: Spain

[Asia | 60]
Capital: Japan
Capital: India
Capital: Thailand
```

## Build
```
make
```
This produces `bin/cram`.

Linux-only (uses `termios`, `select`, and `/dev/urandom`).

## Usage
```
./bin/cram examples/world_countries
```

## Examples
- `examples/world_countries` (capitals by continent)
- `examples/times_tables` (multiplication tables)

## Keys
- `Enter` / `Space` / alphanumeric: next prompt
- `Ctrl+C`: quit

Group changes only apply after the timer expires and you press a key.

## Limits / configuration
Compile-time limits live in `include/config.h`. Defaults:
- `MAX_GROUPS`: 65536
- `MAX_ITEMS_TOTAL`: 1048576 (across all groups)
- `MAX_LINE_LEN`: 65536
- `MAX_FILE_BYTES`: 16 MiB
- `MAX_PROMPTS_PER_RUN`: 1048576
- `MAX_WAIT_LOOPS`: 1048576

If any limit is exceeded, parsing fails with an error.
The program also exits when `MAX_PROMPTS_PER_RUN` is reached.

## Logging
- Writes a timestamped event log to `cram.log` in the current directory (append-only).
- Logged events include: program start/exit, keypresses (raw byte codes), group expiry, prompt display, and reshuffles.
- If the log file cannot be opened, the program continues and prints a warning to stderr.
- No log rotation or size limits are applied.

## Design constraints
- No post-init dynamic allocation.
- Bounded loops with compile-time limits.
- No recursion, no `goto`, no varargs, and no function pointers.

## Error behavior
- Parser errors include line numbers.
- The program fails fast on malformed headers or missing groups/items.
- Terminal raw mode is restored on exit and on errors.
- Input read failures are reported to stderr and logged (if logging is available).

## License
MIT. See `LICENSE`.
