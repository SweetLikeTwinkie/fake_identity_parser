# Use the official MySQL image from the Docker Hub
FROM mysql:latest

# Environment variables to configure MySQL
ENV MYSQL_ROOT_PASSWORD=Tt45PT!@#$
ENV MYSQL_DATABASE=fake_identities_db
ENV MYSQL_USER=twinkie
ENV MYSQL_PASSWORD=slavik12

# Expose the default MySQL port
EXPOSE 3306

# No additional commands are needed, as the official image handles most things