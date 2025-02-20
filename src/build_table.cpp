// #include <print>
// #include <ranges>

#include <charconv>

#include <common.h>
#include <csv_parser.h>
#include <plan.h>
#include <table.h>

template <class Functor>
class TableParser: public CSVParser {
public:
    size_t            row_off_;
    std::vector<Data> last_record_;
    const Attribute*  attributes_data_;
    size_t            attributes_size_;
    Functor           add_record_fn_;

    template <class F>
    TableParser(const std::vector<Attribute>& attributes,
        F&&                                   functor_,
        char                                  escape = '"',
        char sep                                     = ',',
        bool has_trailing_comma                      = false,
        bool has_header                              = false)
    : CSVParser(escape, sep, has_trailing_comma)
    , attributes_data_(attributes.data())
    , attributes_size_(attributes.size())
    , row_off_(has_header ? static_cast<size_t>(-1) : static_cast<size_t>(0))
    , add_record_fn_(static_cast<F&&>(functor_)) {}

    void on_field(size_t col_idx, size_t row_idx, const char* begin, size_t len) override {
        if (row_idx + this->row_off_ == static_cast<size_t>(-1)) {
            return;
        }
        if (len == 0) {
            this->last_record_.emplace_back(std::monostate{});
        } else {
            switch (this->attributes_data_[col_idx].type) {
            case DataType::INT32: {
                int32_t value;
                auto    result = std::from_chars(begin, begin + len, value);
                if (result.ec != std::errc()) {
                    throw std::runtime_error("parse integer error");
                }
                this->last_record_.emplace_back(value);
                break;
            }
            case DataType::INT64: {
                int64_t value;
                auto    result = std::from_chars(begin, begin + len, value);
                if (result.ec != std::errc()) {
                    throw std::runtime_error("parse integer error");
                }
                this->last_record_.emplace_back(value);
                break;
            }
            case DataType::FP64: {
                double value;
                auto   result = std::from_chars(begin, begin + len, value);
                if (result.ec != std::errc()) {
                    throw std::runtime_error("parse float error");
                }
                this->last_record_.emplace_back(value);
                break;
            }
            case DataType::VARCHAR: {
                this->last_record_.emplace_back(std::string{begin, len});
                break;
            }
            }
        }
        if (col_idx + 1 == this->attributes_size_) {
            this->add_record_fn_(std::move(this->last_record_));
            this->last_record_.clear();
            this->last_record_.reserve(attributes_size_);
        }
    }
};

template <class F>
TableParser(const std::vector<Attribute>& attributes,
    F&&                                   functor_,
    char                                  escape = '"',
    char sep                                     = ',',
    bool has_trailing_comma                      = false,
    bool has_header                              = false) -> TableParser<std::decay_t<F>>;

char buffer[1024 * 1024];

std::unordered_map<std::filesystem::path, std::vector<std::vector<Data>>> table_cache;

Table Table::from_csv(const std::vector<Attribute>& attributes,
    const std::filesystem::path&                    path,
    Statement*                                      filter,
    bool                                            header) {
    std::vector<std::vector<Data>> filtered_table;
    if (auto itr = table_cache.find(path); itr != table_cache.end()) {
        const auto& full_table = itr->second;
        if (not filter) {
            filtered_table = full_table;
        } else {
            for (auto& record: full_table) {
                if (filter->eval(record)) {
                    filtered_table.emplace_back(record);
                }
            }
        }
    } else {
        std::vector<std::vector<Data>> full_table;
        File                           fp(path, "rb");
        auto add_record = [&full_table, &filtered_table, attributes, filter](
                              std::vector<Data>&& record) {
            full_table.emplace_back(record);
            if (not filter or filter->eval(record)) {
                filtered_table.emplace_back(std::move(record));
            }
        };
        TableParser parser(attributes, std::move(add_record), '\\', ',', false, header);
        while (true) {
            auto bytes_read = fread(buffer, 1, sizeof(buffer), fp);
            if (bytes_read != 0) {
                auto err = parser.execute(buffer, bytes_read);
                if (err != CSVParser::Ok) {
                    throw std::runtime_error("CSV parse error");
                }
            } else {
                break;
            }
        }
        auto err = parser.finish();
        if (err != CSVParser::Ok) {
            throw std::runtime_error("CSV parse error");
        }
        table_cache.emplace(path, full_table);
    }
    Table table;
    table.set_attributes(attributes);
    table.data_ = std::move(filtered_table);
    return table;
}

bool get_bitmap(const uint8_t* bitmap, uint16_t idx) {
    auto byte_idx = idx / 8;
    auto bit      = idx % 8;
    return bitmap[byte_idx] & (1u << bit);
}

Table Table::from_columnar(const ColumnarTable& table) {
    namespace views = ranges::views;
    std::vector<std::vector<Data>> columns;
    columns.reserve(table.columns.size());
    std::vector<DataType> types;
    types.reserve(table.columns.size());
    for (const auto& [col_idx, column]: table.columns | views::enumerate) {
        types.emplace_back(column.type);
        std::vector<Data> new_column;
        new_column.reserve(table.num_rows);
        for (auto* page:
            column.pages | views::transform([](auto* page) { return page->data; })) {
            switch (column.type) {
            case DataType::INT32: {
                auto  num_rows   = *reinterpret_cast<uint16_t*>(page);
                auto* data_begin = reinterpret_cast<int32_t*>(page + 4);
                auto* bitmap =
                    reinterpret_cast<uint8_t*>(page + PAGE_SIZE - (num_rows + 7) / 8);
                uint16_t data_idx = 0;
                for (uint16_t i = 0; i < num_rows; ++i) {
                    if (get_bitmap(bitmap, i)) {
                        auto value = data_begin[data_idx++];
                        new_column.emplace_back(value);
                    } else {
                        new_column.emplace_back(std::monostate{});
                    }
                }
                break;
            }
            case DataType::INT64: {
                auto  num_rows   = *reinterpret_cast<uint16_t*>(page);
                auto* data_begin = reinterpret_cast<int64_t*>(page + 8);
                auto* bitmap =
                    reinterpret_cast<uint8_t*>(page + PAGE_SIZE - (num_rows + 7) / 8);
                uint16_t data_idx = 0;
                for (uint16_t i = 0; i < num_rows; ++i) {
                    if (get_bitmap(bitmap, i)) {
                        auto value = data_begin[data_idx++];
                        new_column.emplace_back(value);
                    } else {
                        new_column.emplace_back(std::monostate{});
                    }
                }
                break;
            }
            case DataType::FP64: {
                auto  num_rows   = *reinterpret_cast<uint16_t*>(page);
                auto* data_begin = reinterpret_cast<double*>(page + 8);
                auto* bitmap =
                    reinterpret_cast<uint8_t*>(page + PAGE_SIZE - (num_rows + 7) / 8);
                uint16_t data_idx = 0;
                for (uint16_t i = 0; i < num_rows; ++i) {
                    if (get_bitmap(bitmap, i)) {
                        auto value = data_begin[data_idx++];
                        new_column.emplace_back(value);
                    } else {
                        new_column.emplace_back(std::monostate{});
                    }
                }
                break;
            }
            case DataType::VARCHAR: {
                auto num_rows = *reinterpret_cast<uint16_t*>(page);
                if (num_rows == 0xffff) {
                    auto        num_chars  = *reinterpret_cast<uint16_t*>(page + 2);
                    auto*       data_begin = reinterpret_cast<char*>(page + 4);
                    std::string value{data_begin, data_begin + num_chars};
                    new_column.emplace_back(std::move(value));
                } else if (num_rows == 0xfffe) {
                    auto  num_chars  = *reinterpret_cast<uint16_t*>(page + 2);
                    auto* data_begin = reinterpret_cast<char*>(page + 4);
                    std::visit(
                        [data_begin, num_chars](auto& value) {
                            using T = std::decay_t<decltype(value)>;
                            if constexpr (std::is_same_v<T, std::string>) {
                                value.insert(value.end(), data_begin, data_begin + num_chars);
                            } else {
                                throw std::runtime_error(
                                    "long string page 0xfffe must follows a string");
                            }
                        },
                        new_column.back());
                } else {
                    auto  num_non_null = *reinterpret_cast<uint16_t*>(page + 2);
                    auto* offset_begin = reinterpret_cast<uint16_t*>(page + 4);
                    auto* data_begin   = reinterpret_cast<char*>(page + 4 + num_non_null * 2);
                    auto* string_begin = data_begin;
                    auto* bitmap =
                        reinterpret_cast<uint8_t*>(page + PAGE_SIZE - (num_rows + 7) / 8);
                    uint16_t data_idx = 0;
                    for (uint16_t i = 0; i < num_rows; ++i) {
                        if (get_bitmap(bitmap, i)) {
                            auto        offset = offset_begin[data_idx++];
                            std::string value{string_begin, data_begin + offset};
                            string_begin = data_begin + offset;
                            new_column.emplace_back(std::move(value));
                        } else {
                            new_column.emplace_back(std::monostate{});
                        }
                    }
                }
                break;
            }
            }
        }
        columns.emplace_back(std::move(new_column));
    }
    std::vector<std::vector<Data>> results;
    results.reserve(table.num_rows);
    for (size_t i = 0; i < table.num_rows; ++i) {
        std::vector<Data> record;
        record.reserve(table.columns.size());
        for (size_t j = 0; j < table.columns.size(); ++j) {
            record.emplace_back(columns[j][i]);
        }
        results.emplace_back(std::move(record));
    }
    return {results, types};
}

void set_bitmap(std::vector<uint8_t>& bitmap, uint16_t idx) {
    while (bitmap.size() < idx / 8 + 1) {
        bitmap.emplace_back(0);
    }
    auto byte_idx     = idx / 8;
    auto bit          = idx % 8;
    bitmap[byte_idx] |= (1u << bit);
}

void unset_bitmap(std::vector<uint8_t>& bitmap, uint16_t idx) {
    while (bitmap.size() < idx / 8 + 1) {
        bitmap.emplace_back(0);
    }
    auto byte_idx     = idx / 8;
    auto bit          = idx % 8;
    bitmap[byte_idx] &= ~(1u << bit);
}

ColumnarTable Table::to_columnar() const {
    auto& table      = this->data_;
    auto& data_types = this->types_;
    namespace views  = ranges::views;
    ColumnarTable ret;
    ret.num_rows = table.size();
    for (auto [col_idx, data_type]: data_types | views::enumerate) {
        ret.columns.emplace_back(data_type);
        auto& column = ret.columns.back();
        switch (data_type) {
        case DataType::INT32: {
            uint16_t             num_rows = 0;
            std::vector<int32_t> data;
            std::vector<uint8_t> bitmap;
            data.reserve(2048);
            bitmap.reserve(256);
            auto save_page = [&column, &num_rows, &data, &bitmap]() {
                auto* page                             = column.new_page()->data;
                *reinterpret_cast<uint16_t*>(page)     = num_rows;
                *reinterpret_cast<uint16_t*>(page + 2) = static_cast<uint16_t>(data.size());
                memcpy(page + 4, data.data(), data.size() * 4);
                memcpy(page + PAGE_SIZE - bitmap.size(), bitmap.data(), bitmap.size());
                num_rows = 0;
                data.clear();
                bitmap.clear();
            };
            for (auto& record: table) {
                auto& value = record[col_idx];
                std::visit(
                    [&save_page, &column, &num_rows, &data, &bitmap](const auto& value) {
                        using T = std::decay_t<decltype(value)>;
                        if constexpr (std::is_same_v<T, int32_t>) {
                            if (4 + (data.size() + 1) * 4 + (num_rows / 8 + 1) > PAGE_SIZE) {
                                save_page();
                            }
                            set_bitmap(bitmap, num_rows);
                            data.emplace_back(value);
                            ++num_rows;
                        } else if constexpr (std::is_same_v<T, std::monostate>) {
                            if (4 + (data.size()) * 4 + (num_rows / 8 + 1) > PAGE_SIZE) {
                                save_page();
                            }
                            unset_bitmap(bitmap, num_rows);
                            ++num_rows;
                        }
                    },
                    value);
            }
            if (num_rows != 0) {
                save_page();
            }
            break;
        }
        case DataType::INT64: {
            uint16_t             num_rows = 0;
            std::vector<int64_t> data;
            std::vector<uint8_t> bitmap;
            data.reserve(1024);
            bitmap.reserve(128);
            auto save_page = [&column, &num_rows, &data, &bitmap]() {
                auto* page                             = column.new_page()->data;
                *reinterpret_cast<uint16_t*>(page)     = num_rows;
                *reinterpret_cast<uint16_t*>(page + 2) = static_cast<uint16_t>(data.size());
                memcpy(page + 8, data.data(), data.size() * 8);
                memcpy(page + PAGE_SIZE - bitmap.size(), bitmap.data(), bitmap.size());
                num_rows = 0;
                data.clear();
                bitmap.clear();
            };
            for (auto& record: table) {
                auto& value = record[col_idx];
                std::visit(
                    [&save_page, &column, &num_rows, &data, &bitmap](const auto& value) {
                        using T = std::decay_t<decltype(value)>;
                        if constexpr (std::is_same_v<T, int64_t>) {
                            if (8 + (data.size() + 1) * 8 + (num_rows / 8 + 1) > PAGE_SIZE) {
                                save_page();
                            }
                            set_bitmap(bitmap, num_rows);
                            data.emplace_back(value);
                            ++num_rows;
                        } else if constexpr (std::is_same_v<T, std::monostate>) {
                            if (8 + (data.size()) * 8 + (num_rows / 8 + 1) > PAGE_SIZE) {
                                save_page();
                            }
                            unset_bitmap(bitmap, num_rows);
                            ++num_rows;
                        }
                    },
                    value);
            }
            if (num_rows != 0) {
                save_page();
            }
            break;
        }
        case DataType::FP64: {
            uint16_t             num_rows = 0;
            std::vector<double>  data;
            std::vector<uint8_t> bitmap;
            data.reserve(1024);
            bitmap.reserve(128);
            auto save_page = [&column, &num_rows, &data, &bitmap]() {
                auto* page                             = column.new_page()->data;
                *reinterpret_cast<uint16_t*>(page)     = num_rows;
                *reinterpret_cast<uint16_t*>(page + 2) = static_cast<uint16_t>(data.size());
                memcpy(page + 8, data.data(), data.size() * 8);
                memcpy(page + PAGE_SIZE - bitmap.size(), bitmap.data(), bitmap.size());
                num_rows = 0;
                data.clear();
                bitmap.clear();
            };
            for (auto& record: table) {
                auto& value = record[col_idx];
                std::visit(
                    [&save_page, &column, &num_rows, &data, &bitmap](const auto& value) {
                        using T = std::decay_t<decltype(value)>;
                        if constexpr (std::is_same_v<T, double>) {
                            if (8 + (data.size() + 1) * 8 + (num_rows / 8 + 1) > PAGE_SIZE) {
                                save_page();
                            }
                            set_bitmap(bitmap, num_rows);
                            data.emplace_back(value);
                            ++num_rows;
                        } else if constexpr (std::is_same_v<T, std::monostate>) {
                            if (8 + (data.size()) * 8 + (num_rows / 8 + 1) > PAGE_SIZE) {
                                save_page();
                            }
                            unset_bitmap(bitmap, num_rows);
                            ++num_rows;
                        }
                    },
                    value);
            }
            if (num_rows != 0) {
                save_page();
            }
            break;
        }
        case DataType::VARCHAR: {
            uint16_t              num_rows = 0;
            std::vector<char>     data;
            std::vector<uint16_t> offsets;
            std::vector<uint8_t>  bitmap;
            data.reserve(8192);
            offsets.reserve(4096);
            bitmap.reserve(512);
            auto save_long_string = [&column](std::string_view data) {
                size_t offset     = 0;
                auto   first_page = true;
                while (offset < data.size()) {
                    auto* page = column.new_page()->data;
                    if (first_page) {
                        *reinterpret_cast<uint16_t*>(page) = 0xffff;
                        first_page                         = false;
                    } else {
                        *reinterpret_cast<uint16_t*>(page) = 0xfffe;
                    }
                    auto page_data_len = std::min(data.size() - offset, PAGE_SIZE - 4);
                    *reinterpret_cast<uint16_t*>(page + 2) = page_data_len;
                    memcpy(page + 4, data.data() + offset, page_data_len);
                    offset += page_data_len;
                }
            };
            auto save_page = [&column, &num_rows, &data, &offsets, &bitmap]() {
                auto* page                             = column.new_page()->data;
                *reinterpret_cast<uint16_t*>(page)     = num_rows;
                *reinterpret_cast<uint16_t*>(page + 2) = static_cast<uint16_t>(offsets.size());
                memcpy(page + 4, offsets.data(), offsets.size() * 2);
                memcpy(page + 4 + offsets.size() * 2, data.data(), data.size());
                memcpy(page + PAGE_SIZE - bitmap.size(), bitmap.data(), bitmap.size());
                num_rows = 0;
                data.clear();
                offsets.clear();
                bitmap.clear();
            };
            for (auto& record: table) {
                auto& value = record[col_idx];
                std::visit(
                    [&save_long_string,
                        &save_page,
                        &column,
                        &num_rows,
                        &data,
                        &offsets,
                        &bitmap](const auto& value) {
                        using T = std::decay_t<decltype(value)>;
                        if constexpr (std::is_same_v<T, std::string>) {
                            if (value.size() > PAGE_SIZE - 7) {
                                if (num_rows > 0) {
                                    save_page();
                                }
                                save_long_string(value);
                            } else {
                                if (4 + (offsets.size() + 1) * 2 + (data.size() + value.size())
                                        + (num_rows / 8 + 1)
                                    > PAGE_SIZE) {
                                    save_page();
                                }
                                set_bitmap(bitmap, num_rows);
                                data.insert(data.end(), value.begin(), value.end());
                                offsets.emplace_back(data.size());
                                ++num_rows;
                            }
                        } else if constexpr (std::is_same_v<T, std::monostate>) {
                            if (4 + offsets.size() * 2 + data.size() + (num_rows / 8 + 1)
                                > PAGE_SIZE) {
                                save_page();
                            }
                            unset_bitmap(bitmap, num_rows);
                            ++num_rows;
                        } else {
                            throw std::runtime_error("not string or null");
                        }
                    },
                    value);
            }
            if (num_rows != 0) {
                save_page();
            }
            break;
        }
        }
    }
    return ret;
}

void Table::cache(const std::filesystem::path& path,
    const std::vector<std::vector<Data>>&      data_,
    size_t                                     num_cols) {
    File   f(path, "wb");
    size_t num_rows = data_.size();
    fwrite(&num_rows, sizeof(num_rows), 1, f);
    fwrite(&num_cols, sizeof(num_cols), 1, f);
    for (size_t i = 0; i < num_rows; ++i) {
        std::vector<uint8_t> row_bitmap;
        for (size_t j = 0; j < num_cols; ++j) {
            std::visit(
                [&row_bitmap, j](const auto& value) {
                    using T = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<T, std::monostate>) {
                        unset_bitmap(row_bitmap, j);
                    } else {
                        set_bitmap(row_bitmap, j);
                    }
                },
                data_[i][j]);
        }
        fwrite(row_bitmap.data(), 1, row_bitmap.size(), f);
        for (size_t j = 0; j < num_cols; ++j) {
            std::visit(
                [&row_bitmap, j, &f](const auto& value) {
                    using T = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<T, int32_t> or std::is_same_v<T, int64_t>
                                  or std::is_same_v<T, double>) {
                        fwrite(&value, sizeof(T), 1, f);
                    } else if constexpr (std::is_same_v<T, std::string>) {
                        size_t len = value.size();
                        fwrite(&len, sizeof(len), 1, f);
                        fwrite(value.data(), 1, value.size(), f);
                    } else {
                        // std::monostate, do nothing.
                    }
                },
                data_[i][j]);
        }
    }
}

std::vector<std::vector<Data>> Table::load_cache(const std::filesystem::path& path,
    const std::vector<Attribute>&                                             attributes,
    const Statement*                                                          filter) {
    File   f(path, "rb");
    size_t num_rows;
    size_t num_cols;
    std::ignore = fread(&num_rows, sizeof(num_rows), 1, f);
    std::ignore = fread(&num_cols, sizeof(num_cols), 1, f);
    std::vector<std::vector<Data>> ret;
    ret.reserve(num_rows);
    for (size_t i = 0; i < num_rows; ++i) {
        std::vector<uint8_t> row_bitmap((num_cols + 7) / 8);
        std::ignore = fread(row_bitmap.data(), 1, row_bitmap.size(), f);
        std::vector<Data> record;
        record.reserve(num_cols);
        for (size_t j = 0; j < num_cols; ++j) {
            if (get_bitmap(row_bitmap.data(), j)) {
                switch (attributes[j].type) {
                case DataType::INT32: {
                    int32_t value;
                    std::ignore = fread(&value, sizeof(value), 1, f);
                    record.emplace_back(value);
                    break;
                }
                case DataType::INT64: {
                    int64_t value;
                    std::ignore = fread(&value, sizeof(value), 1, f);
                    record.emplace_back(value);
                    break;
                }
                case DataType::FP64: {
                    double value;
                    std::ignore = fread(&value, sizeof(value), 1, f);
                    record.emplace_back(value);
                    break;
                }
                case DataType::VARCHAR: {
                    size_t      len;
                    std::string value;
                    std::ignore = fread(&len, sizeof(len), 1, f);
                    value.resize(len);
                    std::ignore = fread(value.data(), 1, value.size(), f);
                    record.emplace_back(value);
                    break;
                }
                }
            } else {
                record.emplace_back(std::monostate{});
            }
        }
        if (not filter or filter->eval(record)) {
            ret.emplace_back(std::move(record));
        }
    }
    return ret;
}
