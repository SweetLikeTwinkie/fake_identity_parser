CREATE DATABASE IF NOT EXISTS fake_identities_db;
USE fake_identities_db;

CREATE TABLE identities (
    id INT AUTO_INCREMENT PRIMARY KEY,
    name VARCHAR(255) NOT NULL,
    address VARCHAR(255) NOT NULL,
    city VARCHAR(100) NOT NULL,
    state VARCHAR(100) NOT NULL,
    zip_code VARCHAR(20) NOT NULL,
    email VARCHAR(255),
    phone VARCHAR(20),
    username VARCHAR(100) NOT NULL,
    password VARCHAR(100) NOT NULL,
    latitude VARCHAR(15),
    longitude VARCHAR(15),
    user_agent VARCHAR(255),
    gender VARCHAR(10),
    country VARCHAR(10),
    name_set VARCHAR(50)
);