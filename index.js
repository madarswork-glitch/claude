import Anthropic from "@anthropic-ai/sdk";

// Reads ANTHROPIC_API_KEY from the environment — never hardcode your key.
const client = new Anthropic();

async function main() {
  const response = await client.messages.create({
    model: "claude-opus-4-8",
    max_tokens: 1024,
    thinking: { type: "adaptive" },
    messages: [
      { role: "user", content: "In one sentence, say hello to the world." },
    ],
  });

  for (const block of response.content) {
    if (block.type === "text") {
      console.log(block.text);
    }
  }
}

main().catch((err) => {
  console.error(err);
  process.exit(1);
});
