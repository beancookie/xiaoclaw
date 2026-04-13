#!/usr/bin/env python3
"""
FastAPI MCP Server - ESP32 Bridge Compatible

A FastAPI-based MCP server implementing JSON-RPC 2.0 protocol.
Compatible with ESP32 mcp_server.c implementation.

Usage:
    uvicorn scripts.mcp_server:app --host 0.0.0.0 --port 8080

Endpoints:
    GET  /mcp_server  - List available tools (tools/list)
    POST /mcp_server  - Call a tool (tools/call)
"""

import logging
import sys
from datetime import datetime
from typing import Callable

from fastapi import FastAPI
from pydantic import BaseModel

# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(levelname)s - %(message)s',
    stream=sys.stdout
)
logger = logging.getLogger("mcp_server")

# Initialize FastAPI app
app = FastAPI(title="MCP Server", version="1.0.0")

# ============ Tool Registry with Decorator ============

TOOL_REGISTRY: dict[str, dict] = {}


def mcp_tool(name: str, description: str = "", input_schema: dict = None):
    """Decorator to register a tool with the MCP server"""
    def decorator(func: Callable):
        TOOL_REGISTRY[name] = {
            "description": description or func.__doc__ or "",
            "inputSchema": input_schema or {"type": "object", "properties": {}},
            "handler": func,
        }
        return func
    return decorator


# ============ Tool Implementations ============

@mcp_tool(
    name="self.get_time",
    description="Get current time",
    input_schema={"type": "object", "properties": {}}
)
def get_time() -> dict:
    """Get current time"""
    now = datetime.now()
    return {
        "datetime": now.isoformat(),
        "unix_timestamp": int(now.timestamp()),
        "timezone": "UTC",
    }


@mcp_tool(
    name="self.echo",
    description="Echo back the provided message",
    input_schema={
        "type": "object",
        "properties": {"message": {"type": "string"}},
        "required": ["message"],
    }
)
def echo(message: str) -> dict:
    """Echo back the provided message"""
    return {"echo": message, "received_at": datetime.now().isoformat()}


# ============ MCP Protocol Models ============

class JSONRPCRequest(BaseModel):
    jsonrpc: str = "2.0"
    id: int | str | None = None
    method: str
    params: dict | None = None


# ============ FastAPI Endpoints ============

@app.get("/mcp_server")
async def list_tools():
    """List available MCP tools (tools/list)"""
    tools_list = []
    for name, tool in TOOL_REGISTRY.items():
        tools_list.append({
            "name": name,
            "description": tool["description"],
            "inputSchema": tool["inputSchema"],
        })
    return {"tools": tools_list}


@app.post("/mcp_server")
async def call_tool(request: JSONRPCRequest):
    """Call an MCP tool (tools/call or tools/list)"""
    logger.info(f"Received JSON-RPC request: method={request.method}, params={request.params}")

    if request.method == "tools/list":
        tools_list = []
        for name, tool in TOOL_REGISTRY.items():
            tools_list.append({
                "name": name,
                "description": tool["description"],
                "inputSchema": tool["inputSchema"],
            })
        return {"tools": tools_list}

    if request.method != "tools/call":
        return {
            "jsonrpc": "2.0",
            "id": request.id,
            "error": {"code": -32601, "message": f"Method '{request.method}' not found"},
        }

    if not request.params or "name" not in request.params:
        return {
            "jsonrpc": "2.0",
            "id": request.id,
            "error": {"code": -32602, "message": "Missing 'name' in params"},
        }

    tool_name = request.params.get("name")
    tool_args = request.params.get("arguments", {})

    if tool_name not in TOOL_REGISTRY:
        return {
            "jsonrpc": "2.0",
            "id": request.id,
            "error": {"code": -32602, "message": f"Tool '{tool_name}' not found"},
        }

    try:
        result = TOOL_REGISTRY[tool_name]["handler"](**tool_args)
        return {
            "jsonrpc": "2.0",
            "id": request.id,
            "result": {"content": [{"type": "text", "text": str(result)}]},
        }
    except TypeError as e:
        logger.error(f"Tool arguments error: {e}")
        return {
            "jsonrpc": "2.0",
            "id": request.id,
            "error": {"code": -32602, "message": f"Invalid arguments: {str(e)}"},
        }
    except Exception as e:
        logger.error(f"Tool execution error: {e}")
        return {
            "jsonrpc": "2.0",
            "id": request.id,
            "error": {"code": -32603, "message": f"Internal error: {str(e)}"},
        }


if __name__ == "__main__":
    import uvicorn
    uvicorn.run(app, host="0.0.0.0", port=8080)
