# Get Time

Fetch current time from the internet and synchronize the system clock.

## Tool

```
Tool: get_time
Input: {}
```

## Description

- Fetches current time from www.baidu.com via HTTP HEAD request
- Parses the "Date" response header
- Sets the system clock to Beijing Time (UTC+8)
- Returns formatted time string

## Return Format

```
2026-04-07 15:30:45 CST (Tuesday)
```

## Example

```
Tool: get_time
Input: {}
```

Returns:
```
{"datetime": "2026-04-07 15:30:45 CST (Tuesday)"}
```

## Note

- Requires network connectivity
- If proxy is enabled, uses proxy to fetch time
- Otherwise fetches directly from baidu.com
