ARG BUILD_FROM=python:3.12-alpine
FROM $BUILD_FROM

# Copy the uv binary from the official image — no pip required on the base image
COPY --from=ghcr.io/astral-sh/uv:0.10.4 /uv /usr/local/bin/uv

WORKDIR /app

COPY requirements.txt /app/
RUN uv pip install --system --no-cache -r /app/requirements.txt

COPY shot_history.py /app/

CMD ["python3", "-u", "/app/shot_history.py"]
