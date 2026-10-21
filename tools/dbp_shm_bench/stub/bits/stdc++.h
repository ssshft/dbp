// 桩：macOS 的 libc++ 没有 <bits/stdc++.h>（那是 libstdc++ 的私有聚合头）。
// include/shm_spmc_queue.h 里第一行就是它，所以这里补一份等价的聚合。
#pragma once
#include <atomic>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <string>
#include <iostream>
#include <stdexcept>
#include <memory>
#include <vector>
#include <algorithm>
