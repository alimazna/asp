# Session Setup

Every OpenHands container is ephemeral: it does not persist between sessions, so a new
session starts with an empty filesystem and no local copy of this repository. Before any
coordination or code work can happen, the container must clone the repo and set a git
identity, or the agent has nothing to read and nothing to push.

`session-setup.sh` automates exactly that. Given an agent name, it removes any stale
`/workspace/asp`, clones `alimazna/asp` from GitHub using the `GITHUB_TOKEN` in the
environment, and configures the local `user.name` and `user.email` to `<agent>@openhands`
so commits are attributed correctly. It then prints the agent name, the resulting HEAD,
and the remote URL as a sanity check.

Invoke it once at the very start of every session, before any other work:

    bash session-setup.sh <agent-name>

`GITHUB_TOKEN` must be present in the environment and must have read/write access to the
repository — without it the clone fails (and later pushes would fail too). If the script
errors, stop and report; do not work around a missing or invalid token.
