/*
 * Copyright 2025 Matthias Boehm, TU Berlin
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// API of the SIGMOD 2025 Programming Contest,
// See https://sigmod-contest-2025.github.io/index.html
#pragma once

#include <attribute.h>
#include <statement.h>
// #include <table.h>

// supported attribute data types

enum class NodeType {
    HashJoin,
    Scan,
};

struct ScanNode {
    size_t base_table_id;
};

struct JoinNode {
    bool   build_left;
    size_t left;
    size_t right;
    size_t left_attr;
    size_t right_attr;
};

struct PlanNode {
    std::variant<ScanNode, JoinNode>          data;
    std::vector<std::tuple<size_t, DataType>> output_attrs;

    PlanNode(std::variant<ScanNode, JoinNode>     data,
        std::vector<std::tuple<size_t, DataType>> output_attrs)
    : data(std::move(data))
    , output_attrs(std::move(output_attrs)) {}
};

constexpr size_t PAGE_SIZE = 8192;

struct alignas(8) Page {
    std::byte data[PAGE_SIZE];
};

struct Column {
    DataType           type;
    std::vector<Page*> pages;

    Page* new_page() {
        auto ret = new Page;
        pages.push_back(ret);
        return ret;
    }

    Column(DataType data_type)
    : type(data_type)
    , pages() {}

    Column(Column&& other) noexcept
    : type(other.type)
    , pages(std::move(other.pages)) {
        other.pages.clear();
    }

    Column& operator=(Column&& other) noexcept {
        if (this != &other) {
            for (auto* page: pages) {
                delete page;
            }
            type  = other.type;
            pages = std::move(other.pages);
            other.pages.clear();
        }
        return *this;
    }

    Column(const Column&)            = delete;
    Column& operator=(const Column&) = delete;

    ~Column() {
        for (auto* page: pages) {
            delete page;
        }
    }
};

struct ColumnarTable {
    size_t              num_rows{0};
    std::vector<Column> columns;
};

std::tuple<std::vector<std::vector<Data>>, std::vector<DataType>> from_columnar(
    const ColumnarTable& table);
ColumnarTable from_table(const std::vector<std::vector<Data>>& table,
    const std::vector<DataType>&                               data_types);

struct Plan {
    std::vector<PlanNode>      nodes;
    std::vector<ColumnarTable> inputs;
    // std::vector<Table>         tables;
    size_t root;

    size_t new_join_node(bool                     build_left,
        size_t                                    left,
        size_t                                    right,
        size_t                                    left_attr,
        size_t                                    right_attr,
        std::vector<std::tuple<size_t, DataType>> output_attrs) {
        JoinNode join{
            .build_left = build_left,
            .left       = left,
            .right      = right,
            .left_attr  = left_attr,
            .right_attr = right_attr,
        };
        auto ret = nodes.size();
        nodes.emplace_back(join, std::move(output_attrs));
        return ret;
    }

    size_t new_scan_node(size_t                   base_table_id,
        std::vector<std::tuple<size_t, DataType>> output_attrs) {
        ScanNode scan{.base_table_id = base_table_id};
        auto     ret = nodes.size();
        nodes.emplace_back(scan, std::move(output_attrs));
        return ret;
    }

    size_t new_input(ColumnarTable input) {
        auto ret = inputs.size();
        inputs.emplace_back(std::move(input));
        return ret;
    }
};

namespace Contest {

void* build_context();
void  destroy_context(void*);

ColumnarTable execute(const Plan& plan, void* context);

} // namespace Contest
