---
name: skill-creator
description: Create new skills for XiaoClaw
always: false
---
# Skill Creator

Create new skills to extend XiaoClaw's capabilities.

## When to Use

Use this skill when:
- User asks to create a new skill or teach the bot something new
- User wants to add a custom capability or workflow
- User asks to save instructions for later use

## How to Create a Skill

1. Choose a short, descriptive name (lowercase, hyphens allowed)
2. Save a SKILL.md file to `/spiffs/skills/<name>/SKILL.md`
3. Follow this structure:
   - **Frontmatter**: `name` and `description`
   - **Content**: Brief description of what the skill does
   - **When to Use**: Conditions that trigger this skill
   - **How to Use**: Step-by-step instructions

## Skill File Structure

```markdown
---
name: my-skill
description: Brief description of what this skill does
always: false
---
# My Skill

Describe what this skill does here.

## When to Use
When the user wants [specific task].

## How to Use
1. Step one
2. Step two
```

## Best Practices

- Keep skills concise — the context window is limited
- Focus on WHAT to do, not HOW (the agent is smart)
- Use clear, simple language
- Test by asking the agent to use the new skill

## Note on Tool Calling

When you need to create or modify a skill, simply respond describing what you want to do. The system will automatically invoke the appropriate tools through the proper mechanism.
