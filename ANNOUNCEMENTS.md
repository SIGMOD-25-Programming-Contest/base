# Announcements

### 2025-03-13
  - We provide a new header, `hardware.h`. This header contains basic hardware information which enables optimizing for a server's cache sizes or vectorization capabilities.
    - If you miss any information or find issues with the headers, please do not hesitate to contact us.
  - As one of the goals of this contest is to write efficient code for multiple platforms (some of those are kept secret until the final evaluation), we encourage you to read about vector extensions (e.g., Clang's "Vectors and Extended Vectors").
  - We are considering changing the benchmark to include all queries of the standard JOB benchmark. We will reset the leaderboard in this case. We will let you know upfront when this change is about to land.
  - **Third-party libraries:**
    - We want to re-iterate our last notes from 2025-03-04: third-party libraries are **not allowed in your final submission**.
  - **Evaluation workload:**
    -  While there will be a larger variety of queries in the final evaluation workload, we will not add any "surprises". For example, as in the original JoinOrder Benchmark, there will be no joins on string columns.

### 2025-03-04
  - With today's changes to the main repository you forked from, we improved the performance of the evaluation phase
  - **Important notes:**
    - **Deadline change:** The deadline for the final submission has been extended to March 31
    - **Own source files**: The CMake file (which cannot be modified by participants) now includes all *.cpp fiels in the `src` directory. This way, you can add your own source files and better structure your code.
    - **Third-party library:** We found that some teams use third-party libraries, e.g., for  logging. Please note that third-party libraries are not allowed in the contest. You are free to use them during development, but you need to remove them prior to the final submission. Otherwise, your submission is disqualified.

### 2025-02-27
  - The recently pushed GitHub workflow will automatically compile, test, and benchmark your solution on all four systems
  - Check your repository's pull requests
  - The results are currently shown at https://sigmod-contest-25.hpi-sci.de/ and will soon be published on the official contest website
