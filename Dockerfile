FROM python:3.10-slim

WORKDIR /app

# Install dependencies
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt
RUN pip install --no-cache-dir matplotlib

# Copy python scripts
COPY bolus_tracking.py .
COPY batch_process.py .
COPY test_bolus_parity.py .

# Command to run by default if not specified
ENTRYPOINT ["python", "batch_process.py"]
