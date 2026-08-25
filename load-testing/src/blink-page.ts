import autocannon from 'autocannon';

const start = performance.now();
async function runTesting() {
  const instance = autocannon(
    {
      title: 'Load testing for simple esp32 server',
      url: 'http://192.168.1.200/led/blink',
      connections: 5,
      pipelining: 1,
      amount: 2000,
      timeout: 60,
      method: 'POST',
      body: JSON.stringify({ times: 10, intervalMs: 200 }),
    },
    (e, r) => {
      console.log(e);
      console.log(JSON.stringify(r));
      console.log((performance.now() - start) / 1000);
    }
  );
  instance.on('reqError', (err) => {
    console.error('A request failed or timed out:', err);
  });
  autocannon.track(instance, { renderProgressBar: true });
}

await runTesting();
