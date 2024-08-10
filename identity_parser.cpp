#include <iostream>
#include <string>
#include <mysql/mysql.h>
#include <vector>
#include <curl/curl.h>
#include <pugixml.hpp>
#include <regex>
#include <algorithm>
#include <cctype>
#include <locale>
#include <sstream>
#include <tuple>
#include <unordered_map>
#include <ctime>
#include <cstdlib>

/* -----[MySQL Configuration]----- */
const std::string DB_HOST = "172.20.0.2";
const std::string DB_USER = "root";
const std::string DB_PASS = "Tt45PT!@#$";
const std::string DB_NAME = "fake_identities_db";
const unsigned int DB_PORT = 3306;

/* -----[Utility function to trim whitespace from a string]----- */
std::string trim(const std::string& str) {
    auto start = str.begin();
    while (start != str.end() && std::isspace(*start)) {
        start++;
    }

    auto end = str.end();
    do {
        end--;
    } while (std::distance(start, end) > 0 && std::isspace(*end));

    return std::string(start, end + 1);
}

/* -----[Break the address to many fields]----- */
std::tuple<std::string, std::string, std::string, std::string> splitAddress(const std::string& full_address) {
    std::string address, city, state, zip_code;

    // Handle the case where the address contains a <br> tag
    std::size_t br_pos = full_address.find("<br>");
    if (br_pos != std::string::npos) {
        // Extract the address before the <br> tag
        address = full_address.substr(0, br_pos);

        // Extract the city, state, and zip after the <br> tag
        std::string city_state_zip = full_address.substr(br_pos + 4); // +4 to skip "<br>"

        // Now split the city_state_zip into city, state, and zip code
        std::size_t comma_pos = city_state_zip.find(',');
        if (comma_pos != std::string::npos) {
            city = city_state_zip.substr(0, comma_pos);
            std::string state_zip = city_state_zip.substr(comma_pos + 1);
            std::istringstream state_zip_stream(state_zip);
            state_zip_stream >> state >> zip_code; // Separate state and zip code by space
        } else {
            // If no comma is found, assume the entire string is just the city
            city = city_state_zip;
        }
    } else {
        // If no <br> tag, handle it as a single-line address
        address = full_address;
    }

    return std::make_tuple(trim(address), trim(city), trim(state), trim(zip_code));
}

std::string convertStateAbbreviationToFullName(const std::string& abbreviation) {
    static const std::unordered_map<std::string, std::string> stateAbbreviationToName = {
        {"AL", "Alabama"}, {"AK", "Alaska"}, {"AZ", "Arizona"}, {"AR", "Arkansas"},
        {"CA", "California"}, {"CO", "Colorado"}, {"CT", "Connecticut"}, {"DE", "Delaware"},
        {"FL", "Florida"}, {"GA", "Georgia"}, {"HI", "Hawaii"}, {"ID", "Idaho"},
        {"IL", "Illinois"}, {"IN", "Indiana"}, {"IA", "Iowa"}, {"KS", "Kansas"},
        {"KY", "Kentucky"}, {"LA", "Louisiana"}, {"ME", "Maine"}, {"MD", "Maryland"},
        {"MA", "Massachusetts"}, {"MI", "Michigan"}, {"MN", "Minnesota"}, {"MS", "Mississippi"},
        {"MO", "Missouri"}, {"MT", "Montana"}, {"NE", "Nebraska"}, {"NV", "Nevada"},
        {"NH", "New Hampshire"}, {"NJ", "New Jersey"}, {"NM", "New Mexico"}, {"NY", "New York"},
        {"NC", "North Carolina"}, {"ND", "North Dakota"}, {"OH", "Ohio"}, {"OK", "Oklahoma"},
        {"OR", "Oregon"}, {"PA", "Pennsylvania"}, {"RI", "Rhode Island"}, {"SC", "South Carolina"},
        {"SD", "South Dakota"}, {"TN", "Tennessee"}, {"TX", "Texas"}, {"UT", "Utah"},
        {"VT", "Vermont"}, {"VA", "Virginia"}, {"WA", "Washington"}, {"WV", "West Virginia"},
        {"WI", "Wisconsin"}, {"WY", "Wyoming"}
    };

    auto it = stateAbbreviationToName.find(abbreviation);
    if (it != stateAbbreviationToName.end()) {
        return it->second;
    }
    return abbreviation; // Return the abbreviation if not found
}

/* -----[Function to Write Curl Data to String]----- */
size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* s) {
    size_t newLength = size * nmemb;
    try {
        s->append((char*)contents, newLength);
    } 
    catch (std::bad_alloc& e) {
        return 0;
    }
    return newLength;
}

/* -----[Function to construct the URL based on user options]----- */
std::string constructURL(const std::string& gender, const std::string& country, const std::string& nameSet) {
    std::string baseURL = "https://www.fakenamegenerator.com/gen-";
    
    if (nameSet == "common") {
        baseURL += "random";
    } else {
        baseURL += nameSet;
    }

    baseURL += "-";

    if (gender == "male") {
        baseURL += "male";
    } else if (gender == "female") {
        baseURL += "female";
    } else {
        baseURL += "random";
    }

    baseURL += "-";

    baseURL += country;

    baseURL += "-en.php";

    return baseURL;
}

/* -----[Remove common problematic tags]----- */
std::string cleanHTML(const std::string& htmlContent){   
    // - Remove common problematic tags (e.g., <script>, <style>)
    std::regex script_regex("<script[\\s\\S]*?</script>", std::regex::icase);
    std::regex style_regex("<style[\\s\\S]*?</style>", std::regex::icase);
    std::regex meta_regex("<meta[\\s\\S]*?>", std::regex::icase);  
    std::regex link_regex("<link[\\s\\S]*?>", std::regex::icase); 

    std::string cleanedContent = std::regex_replace(htmlContent, script_regex, "");
    cleanedContent = std::regex_replace(cleanedContent, style_regex, "");
    cleanedContent = std::regex_replace(cleanedContent, meta_regex, "");
    cleanedContent = std::regex_replace(cleanedContent, link_regex, "");

    // - Remove potentially broken tags, like incomplete or unclosed tags
    std::regex incomplete_tag_regex("<[a-zA-Z][^>]*?$", std::regex::icase);
    cleanedContent = std::regex_replace(cleanedContent, incomplete_tag_regex, "");

    return cleanedContent;
}

/* -----[Function to parse command-line arguments]----- */
void parseArguments(int argc, char* argv[], std::string& gender, std::string& country, std::string& nameSet, int& amount) {
    // Valid options
    std::vector<std::string> validGenders = {"male", "female", "random"};
    std::vector<std::string> validCountries = {"US", "UK", "CA", "AU", "DE", "FR", "IT", "ES"}; // Add more country codes as needed
    std::vector<std::string> validNameSets = {"common", "arabic", "chinese", "japanese", "russian"}; // Add more name sets as needed

    bool randomizeGender = false;
    bool randomizeCountry = false;
    bool randomizeNameSet = false;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--gender" && i + 1 < argc) {
            gender = argv[++i];
            if (std::find(validGenders.begin(), validGenders.end(), gender) == validGenders.end()) {
                std::cerr << "Invalid gender option. Use male, female, or random." << std::endl;
                exit(1);
            }
        } else if (arg == "--country" && i + 1 < argc) {
            country = argv[++i];
            if (std::find(validCountries.begin(), validCountries.end(), country) == validCountries.end()) {
                std::cerr << "Invalid country option." << std::endl;
                exit(1);
            }
        } else if (arg == "--nameSet" && i + 1 < argc) {
            nameSet = argv[++i];
            if (std::find(validNameSets.begin(), validNameSets.end(), nameSet) == validNameSets.end()) {
                std::cerr << "Invalid name set option." << std::endl;
                exit(1);
            }
        } else if (arg == "--amount" && i + 1 < argc) {
            amount = std::stoi(argv[++i]);
            if (amount <= 0) {
                std::cerr << "Amount must be a positive integer." << std::endl;
                exit(1);
            }
        } else if (arg == "--randomize") {
            randomizeGender = true;
            randomizeCountry = true;
            randomizeNameSet = true;
        } else if (arg == "--randomGender") {
            randomizeGender = true;
        } else if (arg == "--randomCountry") {
            randomizeCountry = true;
        } else if (arg == "--randomNameSet") {
            randomizeNameSet = true;
        } else {
            std::cerr << "Unknown argument: " << arg << std::endl;
            exit(1);
        }
    }

    // If randomization flags are set, randomly select values from valid options
    if (randomizeGender) {
        gender = validGenders[rand() % validGenders.size()];
    }

    if (randomizeCountry) {
        country = validCountries[rand() % validCountries.size()];
    }

    if (randomizeNameSet) {
        nameSet = validNameSets[rand() % validNameSets.size()];
    }
}

/* -----[Function to Fetch HTML from URL]----- */
std::string fetchHTML(const std::string& url){
    CURL* curl;
    CURLcode res;
    std::string readBuffer;

    curl = curl_easy_init();
    if (curl){
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
        res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
    }

    return readBuffer;
}

/* -----[Function to Store Identities in MySQL Database]----- */
void storeInDatabase(const std::vector<std::tuple<std::string, std::string, std::string, std::string, std::string, std::string, std::string, 
                        std::string, std::string, std::string, std::string, std::string, std::string, std::string, std::string>>& identities) {

    /* -----[SQL initialization]----- */
    MYSQL* conn;
    conn = mysql_init(nullptr);
    conn = mysql_real_connect(conn, DB_HOST.c_str(), DB_USER.c_str(), DB_PASS.c_str(), DB_NAME.c_str(), DB_PORT, nullptr, 0);

    if (conn){
        std::cout << "[MySQL] connected successfully!" << std::endl;
        for (const auto& identity : identities){
            std::string name, address, city, state, zip_code, email, phone, username, password, latitude, longitude, user_agent, gender, country, nameSet;
            std::tie(name, address, city, state, zip_code, email, phone, username, password, latitude, longitude, user_agent, gender, country, nameSet) = identity;
            
            std::string query = "INSERT INTO identities (name, address, city, state, zip_code, email, phone, username, password, latitude, longitude, user_agent, gender, country, name_set) VALUES ('" 
                                                    + name + "', '" 
                                                    + address + "', '" 
                                                    + city + "', '" 
                                                    + state + "', '" 
                                                    + zip_code + "', '" 
                                                    + email + "', '" 
                                                    + phone + "', '" 
                                                    + username + "', '" 
                                                    + password + "', '" 
                                                    + latitude + "', '" 
                                                    + longitude + "', '" 
                                                    + user_agent + "', '"
                                                    + gender + "', '" 
                                                    + country + "', '" 
                                                    + nameSet + "')";
            
            if (mysql_query(conn, query.c_str())){
                std::cerr << "[MySQL] Query error failed: " << mysql_error(conn) << std::endl;
            } else {
                std::cout << "[MySQL] Inserted: " << name << std::endl;
            }
        }

        mysql_close(conn);
    } else {
        std::cerr << "[MySQL] Connection to MySQL server failed: " << mysql_error(conn) << std::endl;
    }
}

/* -----[Function to Parse HTML and Extract Identity Information]----- */
std::vector<std::tuple<std::string, std::string, std::string, std::string, std::string, std::string, std::string, std::string, std::string, std::string, std::string, std::string, std::string, std::string, std::string>> parseHTML(
    const std::string& htmlContent, 
    const std::string& gender, 
    const std::string& country, 
    const std::string& nameSet) {
    
    std::vector<std::tuple<std::string, std::string, std::string, std::string, std::string, std::string, std::string, std::string, std::string, std::string, std::string, std::string, std::string, std::string, std::string>> identities;


    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_string(htmlContent.c_str());

    if (!result) {
        std::cerr << "[HTML] Failed to parse HTML content: " << result.description() << std::endl;
        return identities;
    }

    pugi::xpath_node name_node = doc.select_node("//div[@class='address']/h3");
    if (name_node) {
        std::string name = trim(name_node.node().child_value());
        std::cout << "[DEBUG] Parsed Name: " << name << std::endl;

        pugi::xpath_node adr_node = doc.select_node("//div[@class='address']/div[@class='adr']");
        if (adr_node) {
            // Use pugi::xml_node to handle the <br> and split the address
            std::string address, city, state, zip_code;
            pugi::xml_node adr_content = adr_node.node();

            // The first child should be the address
            address = trim(adr_content.first_child().value());

            // The next sibling should be the <br> and after that the city, state, and zip
            std::string city_state_zip = trim(adr_content.last_child().value());

            // Split city_state_zip into city, state, and zip code
            std::size_t comma_pos = city_state_zip.find(',');
            if (comma_pos != std::string::npos) {
                city = city_state_zip.substr(0, comma_pos);
                std::string state_zip = city_state_zip.substr(comma_pos + 1);
                std::istringstream state_zip_stream(state_zip);
                state_zip_stream >> state >> zip_code;
                state = convertStateAbbreviationToFullName(trim(state)); // Convert state to full name
            } else {
                city = city_state_zip;
            }

            std::cout << "[DEBUG] Parsed Address: " << address << std::endl;
            std::cout << "[DEBUG] Parsed City: " << city << std::endl;
            std::cout << "[DEBUG] Parsed State: " << state << std::endl;
            std::cout << "[DEBUG] Parsed Zip Code: " << zip_code << std::endl;

            pugi::xpath_node email_node = doc.select_node("//dt[text()='Email Address']/following-sibling::dd");
            std::string email = email_node ? trim(email_node.node().child_value()) : "";
            std::cout << "[DEBUG] Parsed Email: " << email << std::endl;

            pugi::xpath_node phone_node = doc.select_node("//dt[text()='Phone']/following-sibling::dd");
            std::string phone = phone_node ? trim(phone_node.node().child_value()) : "";
            std::cout << "[DEBUG] Parsed Phone: " << phone << std::endl;

            pugi::xpath_node username_node = doc.select_node("//dt[text()='Username']/following-sibling::dd");
            std::string username = username_node ? trim(username_node.node().child_value()) : "";
            std::cout << "[DEBUG] Parsed Username: " << username << std::endl;

            pugi::xpath_node password_node = doc.select_node("//dt[text()='Password']/following-sibling::dd");
            std::string password = password_node ? trim(password_node.node().child_value()) : "";
            std::cout << "[DEBUG] Parsed Password: " << password << std::endl;

            // Parse Geo Coordinates from <a id="geo">
            pugi::xpath_node geo_node = doc.select_node("//a[@id='geo']");
            std::string latitude, longitude;
            if (geo_node) {
                std::string geo_coordinates = trim(geo_node.node().child_value());
                std::size_t comma_pos = geo_coordinates.find(',');
                if (comma_pos != std::string::npos) {
                    latitude = trim(geo_coordinates.substr(0, comma_pos));
                    longitude = trim(geo_coordinates.substr(comma_pos + 1));
                }
            }
            std::cout << "[DEBUG] Parsed Latitude: " << latitude << std::endl;
            std::cout << "[DEBUG] Parsed Longitude: " << longitude << std::endl;

            // Parse Browser User Agent
            pugi::xpath_node ua_node = doc.select_node("//dt[text()='Browser user agent']/following-sibling::dd");
            std::string user_agent = ua_node ? trim(ua_node.node().child_value()) : "";
            std::cout << "[DEBUG] Parsed User Agent: " << user_agent << std::endl;

            identities.push_back(std::make_tuple(name, address, city, state, zip_code, email, phone, username, password, latitude, longitude, user_agent, gender, country, nameSet));
        }
    }

    return identities;
}

int main(int argc, char* argv[]) {
    /* -----[SQL initialization]----- */
    MYSQL* conn;
    conn = mysql_init(nullptr);

    if (conn) {
        std::cout << "[MySQL] client initialized successfully" << std::endl;
    } else {
        std::cerr << "[MySQL] initialization failed" << std::endl;
        return EXIT_FAILURE;
    }

    conn = mysql_real_connect(conn, DB_HOST.c_str(), 
                                    DB_USER.c_str(), 
                                    DB_PASS.c_str(), 
                                    DB_NAME.c_str(), 
                                    DB_PORT, nullptr, 0);
    
    if (conn){
        std::cout << "[MySQL] Connected to MySQL database successfully!" << std::endl;
        mysql_close(conn);
    } else {
        std::cerr << "[MySQL] Connection to MySQL database failed" << mysql_error(conn) << std::endl;
        return EXIT_FAILURE;
    }

    /* -----[Main Program]----- */

    std::string gender = "male";    // Default gender
    std::string country = "US";     // Default country
    std::string nameSet = "common"; // Default name set
    int amount = 1;                 // Default amount of identities to generate

    // Seed the random number generator
    std::srand(static_cast<unsigned int>(std::time(nullptr)));

    // Parse command-line arguments
    parseArguments(argc, argv, gender, country, nameSet, amount);

    // Debug output to verify the parsed options
    std::cout << "[DEBUG] Gender: " << gender << std::endl;
    std::cout << "[DEBUG] Country: " << country << std::endl;
    std::cout << "[DEBUG] Name Set: " << nameSet << std::endl;
    std::cout << "[DEBUG] Amount: " << amount << std::endl;

    // Construct URL based on user input
    for (int i = 0; i < amount; i++) {
        std::string url = constructURL(gender, country, nameSet);
        std::cout << "[DEBUG] Generated URL: " << url << std::endl;

        std::string htmlContent = fetchHTML(url);
        std::string cleanedHTMLContent = cleanHTML(htmlContent);

        /* Debug Clean HTML by removing problemtatic tags and content */
        //std::cout << cleanedHTMLContent << std::endl; 

        if (!cleanedHTMLContent.empty()) { // Check if there is any HTML content.
            auto identities = parseHTML(cleanedHTMLContent, gender, country, nameSet);

            // Store the parsed identities into the database
            storeInDatabase(identities);
        } else {
            std::cerr << "[HTML] Failed to retrieve HTML content for Identity " << i+1 << std::endl;
        }
    }

    return EXIT_SUCCESS;
}