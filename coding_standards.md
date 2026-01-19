Write all C++ code strictly according to the C++ Core Guidelines and Google C++ Style Guide.
Enforce RAII, const-correctness, ownership clarity, and zero memory/resource leaks.
Apply proven system design patterns and clean architecture principles. Follow modularity approach and minimum coupling.
Use Test-Driven Development: provide unit tests first for all new functionality.
Avoid magic numbers, strings, and implicit assumptions — everything must be named, explicit, and justified.
Prefer readability, determinism, and maintainability over brevity or cleverness.
If a design trade-off exists, explain it briefly before coding.


The most important thing: before doing any code change, think about it in terms of logic.
Analyze system from logical point of view. Investigate the issues in terms of system design.
Does this method need to have such signature?
Is this class accurate in terms of responsibility?
Will I create a non needed coupling?
This is a core of large system. Everything in this repository should be 100% describable and accurate.
If you have any concerns, stop and ask.
Before doing any change you must to have an exactly plan how to implement it.
Like in real project management (development process - Any system should be described in design document first and only after that it could be implemented).
The system should never fail, so make no mistakes. Think.

During fixes ignore LSP errors, trust only build process.
Build always with this steps ONLY (first step could be skipped if this is your first attempt to build or if you didn't add any new dependency):
1. conan install . --build=missing
2. cmake --preset conan-debug
3. cmake --build --preset conan-debug
