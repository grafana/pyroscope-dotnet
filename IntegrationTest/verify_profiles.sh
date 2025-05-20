#!/bin/bash

set -euo pipefail

profilecli_download_url="https://github.com/grafana/pyroscope/releases/download/v1.13.4/profilecli_1.13.4_linux_amd64.tar.gz"
sleep_time=30

echo "Downloading profilecli from $profilecli_url"
curl -L $profilecli_url -o profilecli.tar.gz
tar -xzf profilecli.tar.gz
chmod +x profilecli

echo "Waiting $sleep_time seconds for profile collection..."
sleep $sleep_time

echo "Running profilecli command..."
eval "./profilecli query series --label-names vehicle --query '{service_name=\"rideshare.dotnet.'$1'.'$2'.app\"}'" 2>&1 | grep -v "^level=" | jq -r '.[]' > profilecli_output.json
echo "Labels in profilecli output:"
cat profilecli_output.json
labels=$(cat profilecli_output.json)

# Verify that all expected labels are present
expected_labels=("bike" "car" "scooter")
actual_labels=($(cat profilecli_output.json))

if [ ${#actual_labels[@]} -ne ${#expected_labels[@]} ]; then
  echo "Error: Number of labels doesn't match. Expected ${#expected_labels[@]}, got ${#actual_labels[@]}"
  echo "Actual labels:"
  echo "$labels"
  exit 1
fi

for i in "${!expected_labels[@]}"; do
  if [ "${actual_labels[$i]}" != "${expected_labels[$i]}" ]; then
    echo "Error: Labels don't match at position $i"
    echo "Expected: ${expected_labels[$i]}"
    echo "Got: ${actual_labels[$i]}"
    echo "All actual labels:"
    echo "$labels"
    exit 1
  fi
done

echo "Successfully verified all expected labels (bike, car, scooter) are present"
