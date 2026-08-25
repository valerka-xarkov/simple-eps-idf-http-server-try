import autocannon from 'autocannon';

const start = performance.now();
const testsQuantityInApi = 10000;
async function runTesting() {
  const instance1 = autocannon(
    {
      title: 'Load testing for simple esp32 server',
      // url: 'http://192.168.1.200/cached-in-mem',
      url: 'http://192.168.4.1/cached-in-mem',
      // url: 'http://192.168.4.1/',
      connections: 7,
      pipelining: 1,
      amount: testsQuantityInApi,
      timeout: 60,
    },
    (e, r) => {
      console.log(e);
      console.log(JSON.stringify(r));
      console.log((performance.now() - start) / 1000);
    }
  );
  instance1.on('reqError', (err) => {
    console.error('A request failed or timed out:', err);
  });
  autocannon.track(instance1, { renderProgressBar: true });
}

await runTesting();
