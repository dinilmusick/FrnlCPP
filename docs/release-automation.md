# C++ source release infrastructure

Successful `C++ event dispatch regression` GitHub Actions CI on the exact current main SHA
is a mandatory release gate. Release Please uses Conventional Commits
to propose an updated `version.txt`, changelog and semantic-version
release PR. Merging that PR and passing main CI creates a tagged GitHub
source release; CMake reads that same version.txt.

No local registry token is used. `GITHUB_TOKEN`-created release PRs
do not invoke PR CI automatically; use GitHub App authentication and
mandatory status checks before enabling safe automatic merge.

Tagged GitHub archives are available to CMake FetchContent users, but
this does not yet create published vcpkg ports or Conan binary recipes.
