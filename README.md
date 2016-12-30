# nullserver

A epoll test socket server to get maximum benchmark results for [wrk](https://github.com/wg/wrk) according to this test https://www.techempower.com/benchmarks/#section=code (search "Test type 6: Plaintext").

This server does not met requirements, because it just sends pre-rendered buffer. But the goal is to make it as fast as possible while keeping response acceptable.

# install wrk

```
sudo apt install wrk
```

# clone, build and start

```
git clone https://github.com/loentar/nullserver.git
mkdir -p nullserver-build && cd nullserver-build
cmake -DCMAKE_BUILD_TYPE=Release ../nullserver
make
./nullserver
```

# benchmark with wrk

```
wrk -t8 -c256 -d30s http://localhost:9090/plaintext
```

wrk results with nullserver on Core i5-4460:

```
Running 30s test @ http://localhost:9090/plaintext
  8 threads and 256 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency     0.87ms  271.27us  15.72ms   97.79%
    Req/Sec    37.45k     3.73k  210.62k    97.83%
  8947347 requests in 30.10s, 1.07GB read
Requests/sec: 297250.93
```
