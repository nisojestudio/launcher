FROM python:3.12-slim

ENV PYTHONUNBUFFERED=1
WORKDIR /app

COPY requirements.txt /app/requirements.txt
RUN pip install --no-cache-dir -r /app/requirements.txt

COPY . /app
RUN chmod +x /app/run_bridge.sh

EXPOSE 8770 8766

CMD ["python", "run_tiktok_bridge.py"]
