const [debugPort, pageUrl] = process.argv.slice(2);
if (!debugPort || !pageUrl) {
  console.error("usage: node browser_cdp_wait.mjs DEBUG_PORT PAGE_URL");
  process.exit(2);
}

const target = await fetch(
  `http://127.0.0.1:${debugPort}/json/new?${encodeURIComponent(pageUrl)}`,
  {method: "PUT"},
).then(response => {
  if (!response.ok) throw new Error(`cannot create browser target: ${response.status}`);
  return response.json();
});

const socket = new WebSocket(target.webSocketDebuggerUrl);
await new Promise((resolve, reject) => {
  socket.addEventListener("open", resolve, {once: true});
  socket.addEventListener("error", reject, {once: true});
});

let nextId = 1;
const pending = new Map();
socket.addEventListener("message", event => {
  const message = JSON.parse(event.data);
  if (message.method === "Runtime.consoleAPICalled") {
    const values = message.params.args.map(arg => arg.value ?? arg.description);
    console.error(...values);
  }
  if (!message.id || !pending.has(message.id)) return;
  const {resolve, reject} = pending.get(message.id);
  pending.delete(message.id);
  if (message.error) reject(new Error(message.error.message));
  else resolve(message.result);
});

function command(method, params = {}) {
  const id = nextId++;
  return new Promise((resolve, reject) => {
    pending.set(id, {resolve, reject});
    socket.send(JSON.stringify({id, method, params}));
  });
}

await command("Runtime.enable");
const deadline = Date.now() + 10000;
let status = "pending";
while (Date.now() < deadline) {
  const evaluation = await command("Runtime.evaluate", {
    expression: "document.querySelector('#result')?.dataset.status || 'pending'",
    returnByValue: true,
  });
  status = evaluation.result.value;
  if (status !== "pending") break;
  await new Promise(resolve => setTimeout(resolve, 20));
}

const evaluation = await command("Runtime.evaluate", {
  expression: "document.querySelector('#result')?.textContent || 'NO-RESULT'",
  returnByValue: true,
});
console.log(evaluation.result.value);
try {
  await command("Browser.close");
} catch {
  // Closing the browser may close the protocol socket before its reply arrives.
}
socket.close();
process.exit(status === "pass" ? 0 : 1);
