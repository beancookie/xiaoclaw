---
name: skill-creator
description: Create new skills for XiaoClaw
always: false
---
# Skill Creator

Create new skills for XiaoClaw.

## When to use
When the user asks to create a new skill, teach the bot something, or add a new capability.

## How to create a skill
1. Choose a short, descriptive name (lowercase, hyphens ok)
2. Write a SKILL.md file with this structure:
   - `name` and `description` in frontmatter
   - Brief description paragraph
   - `## When to use` — trigger conditions
   - `## How to use` — step-by-step instructions
   - `## Example` — concrete example (optional but helpful)
3. Save to `/spiffs/skills/<name>/SKILL.md` using write_file
4. The skill will be automatically available after the next conversation

## Best practices
- Keep skills concise — the context window is limited
- Focus on WHAT to do, not HOW (the agent is smart)
- Include specific tool calls the agent should use
- Test by asking the agent to use the new skill

## Example
To create a "translate" skill:
write_file path="/spiffs/skills/translate/SKILL.md" content="---
name: translate
description: Translate text between languages
always: false
---
# Translate

Translate text between languages.

## When to use
When the user asks to translate text.

## How to use
1. Identify source and target languages
2. Translate directly using your language knowledge
3. For specialized terms, use web_search to verify
"

## How to add an MCP Server

To connect to a remote MCP server for additional tools:

1. First, read the available servers from mcp-servers.md:
   read_file path="/spiffs/mcp-servers.md"

2. If your server is not listed, add it to mcp-servers.md:
   write_file path="/spiffs/mcp-servers.md" content="# MCP Servers

## my_server
- host: <server-ip-address>
- port: <server-port>
- endpoint: mcp

## another_server
- host: 10.0.0.50
- port: 9000
- endpoint: mcp
"

3. Connect using the mcp_connect tool:
   mcp_connect {"server_name": "my_server"}

4. After connecting, the server's tools will be dynamically registered and available with the prefix "{server_name}." (e.g., my_server.get_status)

5. To disconnect:
   mcp_disconnect {}

## MCP Server Parameters
- **host**: IP address or hostname of the MCP server
- **port**: HTTP port (default: 8000)
- **endpoint**: HTTP path endpoint (default: mcp)

## Example
User: "Add an MCP server at 192.168.1.100 port 8000"
Agent:
1. write_file path="/spiffs/mcp-servers.md" content="# MCP Servers

## my_server
- host: 192.168.1.100
- port: 8000
- endpoint: mcp
"
2. mcp_connect {"server_name": "my_server"}
3. Now tools like my_server.get_status are available

## How to create and run Lua scripts

XiaoClaw supports Lua scripting. You can create Lua scripts and execute them.

### Create a Lua script
1. Use write_file to save the script to `/spiffs/lua/<name>.lua`
2. Scripts should return a value or use print() for output

### Run Lua code directly
Use lua_eval to execute a code snippet:
lua_eval {"code": "return 2 + 2"}  -- returns {"result": 4}

### Run a saved Lua script
Use lua_run to execute a stored script:
lua_run {"path": "/spiffs/lua/myscript.lua"}

### Example: Creating a Lua script
User: "Create a Lua script that calculates fibonacci"
Agent: write_file path="/spiffs/lua/fibonacci.lua" content="-- Fibonacci function
local function fib(n)
    if n <= 1 then return n end
    return fib(n-1) + fib(n-2)
end

-- Calculate and return result
return fib(10)"

### Example: Running the script
User: "Run the fibonacci script"
Agent: lua_run {"path": "/spiffs/lua/fibonacci.lua"}

### Lua available libraries
- print() - Output text
- string library - String operations
- math library - Math functions
- table library - Table operations
- coroutine library - Coroutines
- os library - Limited (time only)
