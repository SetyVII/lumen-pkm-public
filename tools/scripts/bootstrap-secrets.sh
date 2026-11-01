#!/usr/bin/env bash
set -euo pipefail
umask 077

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
workspace_root="$(cd -- "$script_dir/../../.." && pwd)"
secret_dir="${1:-$workspace_root/ops/personal/secrets}"
mkdir -p "$secret_dir"
secret_dir="$(realpath "$secret_dir")"
chmod 0700 "$secret_dir"
image="${LUMEN_IMAGE:-lumen-pkm:local}"
docker_os="$(docker info --format '{{.OperatingSystem}}')"

if [ -e "$secret_dir/encryption.key" ] || [ -e "$secret_dir/api.token" ]; then
  printf '%s\n' 'Secrets already exist. No key or token was replaced. See README for permission repair.' >&2
  exit 1
fi

# Runtime UID is fixed at 1000. Docker Desktop projects bind ownership to the
# container's selected user and does not support chown on these virtual mounts.
# Native Docker does need the real numeric owner. :z labels only this private
# directory when a native SELinux-enabled engine supports relabelling.
if [[ "$docker_os" == *"Docker Desktop"* ]]; then
  docker run --rm --user 0:0 \
    --volume "$secret_dir:/secrets:z" \
    --entrypoint /bin/sh "$image" -c \
    '/app/lumen -init-secrets /secrets && chmod 0400 /secrets/encryption.key /secrets/api.token'
  # Desktop's virtual bind can report a chmod that is not reflected on the
  # host. These files belong to the host operator, so enforce its real modes.
  chmod 0400 "$secret_dir/encryption.key" "$secret_dir/api.token"
else
  docker run --rm --user 0:0 \
    --volume "$secret_dir:/secrets:z" \
    --entrypoint /bin/sh "$image" -c \
    '/app/lumen -init-secrets /secrets && chown 1000:1000 /secrets/encryption.key /secrets/api.token && chmod 0400 /secrets/encryption.key /secrets/api.token'
fi

# Check the same individual file mounts that Compose uses, rather than assuming
# a successful helper proves the unprivileged runtime can read its secrets.
docker run --rm --user 1000:1000 \
  --mount "type=bind,source=$secret_dir/encryption.key,target=/run/secrets/encryption_key,readonly" \
  --mount "type=bind,source=$secret_dir/api.token,target=/run/secrets/api_token,readonly" \
  --entrypoint /bin/sh "$image" -c \
  'test -r /run/secrets/encryption_key && test -r /run/secrets/api_token'

# A separate client token copy belongs to the host operator. The encryption
# key stays owned by the server runtime and is never copied into client config.
docker run --rm --user 0:0 \
  --mount "type=bind,source=$secret_dir,target=/secrets,readonly" \
  --entrypoint /bin/cat "$image" /secrets/api.token > "$secret_dir/client.token"
chmod 0600 "$secret_dir/client.token"
printf '%s\n' 'Server runtime UID 1000 can read its secrets, mode 0400. Client token: your host UID, mode 0600. No secret was printed.'
