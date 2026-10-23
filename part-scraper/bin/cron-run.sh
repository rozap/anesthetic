#!/bin/bash
# Pulls the repo, then runs every car-part config in sequence. The runs share
# .camoufox-profile and data/seen.json, so they must not overlap; that is why
# this is one script and not two crontab entries.
#
# Everything is inside main() so bash parses the whole script before the pull
# can rewrite this file underneath it.
set -u

main() {
  cd "$(dirname "$0")/.." || exit 1
  export PATH="$HOME/.local/node/bin:$HOME/.asdf/shims:$PATH"
  [ -f "$HOME/.config/carpart.env" ] && . "$HOME/.config/carpart.env"
  mkdir -p log

  echo "=== $(date -Is) git pull"
  git pull --ff-only --quiet || echo "=== git pull exited $?, running the checked-out code"

  # Reinstall only when the lockfile changed since the last install.
  local lock_sum
  lock_sum=$(sha1sum package-lock.json | cut -d' ' -f1)
  if [ "$lock_sum" != "$(cat node_modules/.lock-sha1 2>/dev/null)" ]; then
    echo "=== $(date -Is) npm ci"
    npm ci --silent && echo "$lock_sum" > node_modules/.lock-sha1
  fi

  for cfg in 32-vr6.config.json volvo-v70xc.config.json; do
    echo "=== $(date -Is) $cfg"
    npm start --silent -- --headed --config "$cfg" || echo "=== $cfg exited $?"
  done
}

main "$@"
