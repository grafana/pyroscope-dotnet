FROM python:3.14@sha256:151ab3571dad616bb031052e86411e2165295c7f67ef27206852203e854bcd12

RUN pip3 install requests

COPY load-generator.py ./load-generator.py

ENV PYTHONUNBUFFERED=1

CMD [ "python", "load-generator.py" ]
