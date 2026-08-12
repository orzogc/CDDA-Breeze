# Dialogue response patch API

Breeze supports stable identifiers and load-order patches for JSON dialogue responses.

A response can opt into a stable identifier with `response_id`.

```json
{
  "id": "TALK_EXAMPLE",
  "type": "talk_topic",
  "responses": [
    { "response_id": "trade", "text": "Trade.", "topic": "TALK_TRADE" },
    { "response_id": "leave", "text": "Goodbye.", "topic": "TALK_DONE" }
  ]
}
```

A later-loaded mod can patch the same topic without copying the original response array.

```json
{
  "id": "TALK_EXAMPLE",
  "type": "talk_topic",
  "responses": [],
  "response_patches": [
    {
      "op": "insert_before",
      "target": "trade",
      "response": { "response_id": "factory", "text": "About the factory.", "topic": "TALK_FACTORY" }
    },
    { "op": "move_after", "target": "factory", "anchor": "trade" },
    { "op": "delete", "target": "obsolete_question" },
    {
      "op": "replace",
      "target": "leave",
      "response": { "text": "That's all.", "topic": "TALK_DONE" }
    }
  ]
}
```

Supported operations are `insert_before`, `insert_after`, `delete`, `replace`, `move_before`, and `move_after`.
`replace` keeps the target identifier when the replacement omits `response_id`.
Targets that do not exist are skipped with a warning so a mod does not crash merely because another mod changed the same dialogue.
Patches are applied in normal mod load order.

Breeze also accepts upstream `insert_before_standard_exits`. It places newly appended responses above a trailing `TALK_NONE` / `TALK_DONE` pair when present.

Existing responses without `response_id` continue to work unchanged. They simply cannot be addressed by a stable response patch until an identifier is added.
