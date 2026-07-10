FROM vvakame/review:5.9

WORKDIR /book

COPY . /book

RUN ./setup.sh
