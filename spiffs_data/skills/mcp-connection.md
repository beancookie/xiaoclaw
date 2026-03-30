# MCP Server Connection

## Connection
- host: 192.168.1.100
- port: 8000
- endpoint: mcp
- timeout_ms: 10000
- enabled: true

## Description
Configure the connection to a remote MCP server.

- **host**: IP address or hostname of the MCP server
- **port**: HTTP port of the MCP server
- **endpoint**: HTTP path endpoint (e.g., "mcp" for /mcp)
- **timeout_ms**: Timeout for tool calls in milliseconds
- **enabled**: Set to "true" to enable MCP client, "false" to disable

When enabled, the MCP client will connect to the specified server at startup,
discover available tools, and register them for the LLM to use.
