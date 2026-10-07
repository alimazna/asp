#!/bin/bash
# Run this first in any new OpenHands container.
# Usage: bash session-setup.sh <agent-name>
set -e

AGENT_NAME="${1:-agent}"
REPO="alimazna/asp"

cd /workspace
rm -rf asp
git clone "https://${GITHUB_TOKEN}@github.com/${REPO}.git" asp
cd asp

git config user.email "${AGENT_NAME}@openhands"
git config user.name "${AGENT_NAME}"

echo "Setup complete."
echo "Agent: ${AGENT_NAME}"
echo "HEAD:  $(git log --oneline -1)"
echo "Remote: $(git remote -v | head -1)"
