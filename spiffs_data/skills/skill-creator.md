# Skill Creator

Create new skills for MimiClaw.

## When to use
When the user asks to create a new skill, teach the bot something, or add a new capability.

## How to create a skill
1. Choose a short, descriptive name (lowercase, hyphens ok)
2. Write a SKILL.md file with this structure:
   - `# Title` — clear name
   - Brief description paragraph
   - `## When to use` — trigger conditions
   - `## How to use` — step-by-step instructions
   - `## Example` — concrete example (optional but helpful)
3. Save to `/spiffs/skills/<name>.md` using write_file
4. The skill will be automatically available after the next conversation

## Best practices
- Keep skills concise — the context window is limited
- Focus on WHAT to do, not HOW (the agent is smart)
- Include specific tool calls the agent should use
- Test by asking the agent to use the new skill

## Example
To create a "translate" skill:
write_file path="/spiffs/skills/translate.md" content="# Translate\n\nTranslate text between languages.\n\n## When to use\nWhen the user asks to translate text.\n\n## How to use\n1. Identify source and target languages\n2. Translate directly using your language knowledge\n3. For specialized terms, use web_search to verify\n"

## How to add an MCP Server

To connect to a remote MCP server for additional tools:

1. Create the connection config file:
   write_file path="/spiffs/skills/mcp-connection.md" content="# MCP Server Connection\n\n## Connection\n- host: <server-ip-address>\n- port: <server-port>\n- endpoint: mcp\n- timeout_ms: 10000\n- enabled: true\n\n## Description\nConfigure the connection to a remote MCP server.\n"

2. Available parameters:
   - **host**: IP address or hostname of the MCP server
   - **port**: HTTP port (default: 8000)
   - **endpoint**: HTTP path endpoint (default: mcp)
   - **timeout_ms**: Tool call timeout in milliseconds (default: 10000)
   - **enabled**: "true" to enable, "false" to disable

3. After creating the file, the MCP client will automatically connect on next startup

## Example
User: "Add an MCP server at 192.168.1.100 port 8000"
Agent: writes_file path="/spiffs/skills/mcp-connection.md" content="# MCP Server Connection\n\n## Connection\n- host: 192.168.1.100\n- port: 8000\n- endpoint: mcp\n- timeout_ms: 10000\n- enabled: true\n..."
