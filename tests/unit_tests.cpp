#include <catch2/catch_test_macros.hpp>

#include <table.h>
#include <plan.h>

void sort(std::vector<std::vector<Data>>& table) {
    std::sort(table.begin(), table.end());
}

TEST_CASE("Empty join", "[join]") {
    Plan plan;
    plan.new_scan_node(0, {{0, DataType::INT32}});
    plan.new_scan_node(1, {{0, DataType::INT32}});
    plan.new_join_node(true, 0, 1, 0, 0, {{0, DataType::INT32}, {1, DataType::INT32}});
    ColumnarTable table1, table2;
    table1.columns.emplace_back(DataType::INT32);
    table2.columns.emplace_back(DataType::INT32);
    plan.inputs.emplace_back(std::move(table1));
    plan.inputs.emplace_back(std::move(table2));
    plan.root = 2;
    auto* context = Contest::build_context();
    auto result = Contest::execute(plan, context);
    Contest::destroy_context(context);
    REQUIRE(result.num_rows == 0);
    REQUIRE(result.columns.size() == 2);
    REQUIRE(result.columns[0].type == DataType::INT32);
    REQUIRE(result.columns[1].type == DataType::INT32);
}

TEST_CASE("One line join", "[join]") {
    Plan plan;
    plan.new_scan_node(0, {{0, DataType::INT32}});
    plan.new_scan_node(1, {{0, DataType::INT32}});
    plan.new_join_node(true, 0, 1, 0, 0, {{0, DataType::INT32}, {1, DataType::INT32}});
    std::vector<std::vector<Data>> data{
        {1, },
    };
    std::vector<DataType> types{DataType::INT32};
    Table table(std::move(data), std::move(types));
    ColumnarTable table1 = table.to_columnar();
    ColumnarTable table2 = table.to_columnar();
    plan.inputs.emplace_back(std::move(table1));
    plan.inputs.emplace_back(std::move(table2));
    plan.root = 2;
    auto* context = Contest::build_context();
    auto result = Contest::execute(plan, context);
    Contest::destroy_context(context);
    REQUIRE(result.num_rows == 1);
    REQUIRE(result.columns.size() == 2);
    REQUIRE(result.columns[0].type == DataType::INT32);
    REQUIRE(result.columns[1].type == DataType::INT32);
    auto result_table = Table::from_columnar(result);
    std::vector<std::vector<Data>> ground_truth{
        {1, 1,},
    };
    REQUIRE(result_table.table() == ground_truth);
}

TEST_CASE("Simple join", "[join]") {
    Plan plan;
    plan.new_scan_node(0, {{0, DataType::INT32}});
    plan.new_scan_node(1, {{0, DataType::INT32}});
    plan.new_join_node(true, 0, 1, 0, 0, {{0, DataType::INT32}, {1, DataType::INT32}});
    std::vector<std::vector<Data>> data{
        {1,},
        {2,},
        {3,},
    };
    std::vector<DataType> types{DataType::INT32};
    Table table(std::move(data), std::move(types));
    ColumnarTable table1 = table.to_columnar();
    ColumnarTable table2 = table.to_columnar();
    plan.inputs.emplace_back(std::move(table1));
    plan.inputs.emplace_back(std::move(table2));
    plan.root = 2;
    auto* context = Contest::build_context();
    auto result = Contest::execute(plan, context);
    Contest::destroy_context(context);
    REQUIRE(result.num_rows == 3);
    REQUIRE(result.columns.size() == 2);
    REQUIRE(result.columns[0].type == DataType::INT32);
    REQUIRE(result.columns[1].type == DataType::INT32);
    auto result_table = Table::from_columnar(result);
    std::vector<std::vector<Data>> ground_truth{
        {1, 1,},
        {2, 2,},
        {3, 3,},
    };
    sort(result_table.table());
    REQUIRE(result_table.table() == ground_truth);
}

TEST_CASE("Empty Result", "[join]") {
    Plan plan;
    plan.new_scan_node(0, {{0, DataType::INT32}});
    plan.new_scan_node(1, {{0, DataType::INT32}});
    plan.new_join_node(true, 0, 1, 0, 0, {{0, DataType::INT32}, {1, DataType::INT32}});
    std::vector<std::vector<Data>> data1{
        {1,},
        {2,},
        {3,},
    };
    std::vector<std::vector<Data>> data2{
        {4,},
        {5,},
        {6,},
    };
    std::vector<DataType> types{DataType::INT32};
    Table table1(std::move(data1), types);
    Table table2(std::move(data2), std::move(types));
    ColumnarTable input1 = table1.to_columnar();
    ColumnarTable input2 = table2.to_columnar();
    plan.inputs.emplace_back(std::move(input1));
    plan.inputs.emplace_back(std::move(input2));
    plan.root = 2;
    auto* context = Contest::build_context();
    auto result = Contest::execute(plan, context);
    Contest::destroy_context(context);
    REQUIRE(result.num_rows == 0);
    REQUIRE(result.columns.size() == 2);
    REQUIRE(result.columns[0].type == DataType::INT32);
    REQUIRE(result.columns[1].type == DataType::INT32);
}

TEST_CASE("Multiple same keys", "[join]") {
    Plan plan;
    plan.new_scan_node(0, {{0, DataType::INT32}});
    plan.new_scan_node(1, {{0, DataType::INT32}});
    plan.new_join_node(true, 0, 1, 0, 0, {{0, DataType::INT32}, {1, DataType::INT32}});
    std::vector<std::vector<Data>> data1{
        {1,},
        {1,},
        {2,},
        {3,},
    };
    std::vector<DataType> types{DataType::INT32};
    Table table1(std::move(data1), std::move(types));
    ColumnarTable input1 = table1.to_columnar();
    ColumnarTable input2 = table1.to_columnar();
    plan.inputs.emplace_back(std::move(input1));
    plan.inputs.emplace_back(std::move(input2));
    plan.root = 2;
    auto* context = Contest::build_context();
    auto result = Contest::execute(plan, context);
    Contest::destroy_context(context);
    REQUIRE(result.num_rows == 6);
    REQUIRE(result.columns.size() == 2);
    REQUIRE(result.columns[0].type == DataType::INT32);
    REQUIRE(result.columns[1].type == DataType::INT32);
    auto result_table = Table::from_columnar(result);
    std::vector<std::vector<Data>> ground_truth{
        {1, 1,},
        {1, 1,},
        {1, 1,},
        {1, 1,},
        {2, 2,},
        {3, 3,},
    };
    sort(result_table.table());
    REQUIRE(result_table.table() == ground_truth);
}

TEST_CASE("NULL keys", "[join]") {
    Plan plan;
    plan.new_scan_node(0, {{0, DataType::INT32}});
    plan.new_scan_node(1, {{0, DataType::INT32}});
    plan.new_join_node(true, 0, 1, 0, 0, {{0, DataType::INT32}, {1, DataType::INT32}});
    std::vector<std::vector<Data>> data1{
        {1,               },
        {1,               },
        {std::monostate{},},
        {2,               },
        {3,               },
    };
    std::vector<DataType> types{DataType::INT32};
    Table table1(std::move(data1), std::move(types));
    ColumnarTable input1 = table1.to_columnar();
    ColumnarTable input2 = table1.to_columnar();
    plan.inputs.emplace_back(std::move(input1));
    plan.inputs.emplace_back(std::move(input2));
    plan.root = 2;
    auto* context = Contest::build_context();
    auto result = Contest::execute(plan, context);
    Contest::destroy_context(context);
    REQUIRE(result.num_rows == 6);
    REQUIRE(result.columns.size() == 2);
    REQUIRE(result.columns[0].type == DataType::INT32);
    REQUIRE(result.columns[1].type == DataType::INT32);
    auto result_table = Table::from_columnar(result);
    std::vector<std::vector<Data>> ground_truth{
        {1, 1,},
        {1, 1,},
        {1, 1,},
        {1, 1,},
        {2, 2,},
        {3, 3,},
    };
    sort(result_table.table());
    REQUIRE(result_table.table() == ground_truth);
}

TEST_CASE("Multiple columns", "[join]") {
    Plan plan;
    plan.new_scan_node(0, {{0, DataType::INT32}});
    plan.new_scan_node(1, {{1, DataType::VARCHAR}, {0, DataType::INT32}});
    plan.new_join_node(true, 0, 1, 0, 1, {{0, DataType::INT32}, {2, DataType::INT32}, {1, DataType::VARCHAR}});
    using namespace std::string_literals;
    std::vector<std::vector<Data>> data1{
        {1               , "xxx"s,},
        {1               , "yyy"s,},
        {std::monostate{}, "zzz"s,},
        {2               , "uuu"s,},
        {3               , "vvv"s,},
    };
    std::vector<DataType> types{DataType::INT32, DataType::VARCHAR};
    Table table1(std::move(data1), std::move(types));
    ColumnarTable input1 = table1.to_columnar();
    ColumnarTable input2 = table1.to_columnar();
    plan.inputs.emplace_back(std::move(input1));
    plan.inputs.emplace_back(std::move(input2));
    plan.root = 2;
    auto* context = Contest::build_context();
    auto result = Contest::execute(plan, context);
    Contest::destroy_context(context);
    REQUIRE(result.num_rows == 6);
    REQUIRE(result.columns.size() == 3);
    REQUIRE(result.columns[0].type == DataType::INT32);
    REQUIRE(result.columns[1].type == DataType::INT32);
    REQUIRE(result.columns[2].type == DataType::VARCHAR);
    auto result_table = Table::from_columnar(result);
    std::vector<std::vector<Data>> ground_truth{
        {1, 1, "xxx"s},
        {1, 1, "xxx"s},
        {1, 1, "yyy"s},
        {1, 1, "yyy"s},
        {2, 2, "uuu"s},
        {3, 3, "vvv"s},
    };
    sort(result_table.table());
    REQUIRE(result_table.table() == ground_truth);
}

TEST_CASE("Build on right", "[join]") {
    Plan plan;
    plan.new_scan_node(0, {{0, DataType::INT32}});
    plan.new_scan_node(1, {{1, DataType::VARCHAR}, {0, DataType::INT32}});
    plan.new_join_node(false, 0, 1, 0, 1, {{0, DataType::INT32}, {2, DataType::INT32}, {1, DataType::VARCHAR}});
    using namespace std::string_literals;
    std::vector<std::vector<Data>> data1{
        {1               , "xxx"s,},
        {1               , "yyy"s,},
        {std::monostate{}, "zzz"s,},
        {2               , "uuu"s,},
        {3               , "vvv"s,},
    };
    std::vector<DataType> types{DataType::INT32, DataType::VARCHAR};
    Table table1(std::move(data1), std::move(types));
    ColumnarTable input1 = table1.to_columnar();
    ColumnarTable input2 = table1.to_columnar();
    plan.inputs.emplace_back(std::move(input1));
    plan.inputs.emplace_back(std::move(input2));
    plan.root = 2;
    auto* context = Contest::build_context();
    auto result = Contest::execute(plan, context);
    Contest::destroy_context(context);
    REQUIRE(result.num_rows == 6);
    REQUIRE(result.columns.size() == 3);
    REQUIRE(result.columns[0].type == DataType::INT32);
    REQUIRE(result.columns[1].type == DataType::INT32);
    REQUIRE(result.columns[2].type == DataType::VARCHAR);
    auto result_table = Table::from_columnar(result);
    std::vector<std::vector<Data>> ground_truth{
        {1, 1, "xxx"s},
        {1, 1, "xxx"s},
        {1, 1, "yyy"s},
        {1, 1, "yyy"s},
        {2, 2, "uuu"s},
        {3, 3, "vvv"s},
    };
    sort(result_table.table());
    REQUIRE(result_table.table() == ground_truth);
}
