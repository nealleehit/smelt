#include <iostream>
#include <string>
#include <vector>
#include <catch/catch.hpp>
#include <nlohmann_json/json.hpp>
#include "json_object.h"

TEST_CASE("Test JSON object wrapper", "[Helpers][Json]") { 
  SECTION("Test JSON object construction") {
    utilities::JsonObject test_object;

    REQUIRE(test_object.is_empty());
    REQUIRE_THROWS(test_object.delete_key("something"));

    test_object.add_value("Foo", 17);
    REQUIRE(test_object.get_value<int>("Foo") == 17);
    REQUIRE(!test_object.is_empty());

    test_object.delete_key("Foo");
    REQUIRE(test_object.is_empty());
    REQUIRE_THROWS(test_object.get_value<int>("Foo"));

    std::vector<double> double_vec = {0.0, 1.0, 2.0};
    test_object.add_value("Vector", double_vec);
    test_object.add_value("String", "I'm a string");
    test_object.add_value("String", "Yet another string");
    test_object.add_value("Vector", double_vec);
    REQUIRE(test_object.get_size() == 2);    

    utilities::JsonObject insert_object;
    insert_object.add_value("Bar", true);
    
    try {
      insert_object.add_as_value("JSON Object", test_object);
    } catch (const std::exception& e) {
      std::cerr << e.what();
      FAIL("Failed to add JSON object as value in JsonObject\n");
    }

    test_object.clear();
    REQUIRE(test_object.is_empty());
  }
}