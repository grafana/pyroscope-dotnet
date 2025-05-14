import random
import requests
import time
import os

HOST = os.environ.get('SERVICE_NAME', '')

VEHICLES = [
    'bike',
    'scooter',
    'car',
]

if __name__ == "__main__":
    print(f"starting load generator")
    time.sleep(3)
    counter = 0
    while True:
        vehicle = VEHICLES[counter % len(VEHICLES)]
        counter += 1
        print(f"requesting {vehicle} from {HOST}")
        resp = requests.get(f'http://{HOST}:5000/{vehicle}')
        print(f"received {resp}")
        time.sleep(random.uniform(0.2, 0.4))
