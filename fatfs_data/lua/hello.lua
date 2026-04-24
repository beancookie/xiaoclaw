-- Hello World Lua Script
-- This is a simple test script for XiaoClaw's Lua support

-- Return a greeting message
local greeting = "Hello from Lua!"
local timestamp = os.time()

return string.format("%s (timestamp: %d)", greeting, timestamp)
