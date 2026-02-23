ARG BUILD_FROM=ghcr.io/home-assistant/amd64-base-python:3.12
FROM $BUILD_FROM

WORKDIR /app

COPY requirements.txt /app/
RUN python3 -m pip install --no-cache-dir -r /app/requirements.txt

COPY shot_history.py /app/

CMD ["python3", "-u", "/app/shot_history.py"]
