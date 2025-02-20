#pragma once

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <variant>
#include <vector>
#include <unordered_map>

#include <fmt/core.h>
#include <re2/re2.h>

using Data    = std::variant<int32_t, int64_t, double, std::string, std::monostate>;
using Literal = std::variant<int64_t, double, std::string, std::monostate>;

struct Attribute;
struct Statement;
struct Comparison;
struct LogicalOperation;

// AST Node
struct Statement {
    virtual ~Statement()                                            = default;
    virtual std::string pretty_print(int indent = 0) const          = 0;
    virtual bool        eval(const std::vector<Data>& record) const = 0;
};

struct Comparison: Statement {
    size_t column;

    enum Op {
        EQ,
        NEQ,
        LT,
        GT,
        LEQ,
        GEQ,
        LIKE,
        NOT_LIKE,
        IS_NULL,
        IS_NOT_NULL
    };

    Op      op;
    Literal value;

    Comparison(size_t col, Op o, Literal val)
    : column(col)
    , op(o)
    , value(std::move(val)) {}

    std::string pretty_print(int indent) const override {
        return fmt::format("{:{}}{} {} {}", "", indent, column, opToString(), valueToString());
    }

    bool eval(const std::vector<Data>& record) const override;

private:
    std::string opToString() const {
        switch (op) {
        case EQ:          return "=";
        case NEQ:         return "!=";
        case LT:          return "<";
        case GT:          return ">";
        case LEQ:         return "<=";
        case GEQ:         return ">=";
        case LIKE:        return "LIKE";
        case NOT_LIKE:    return "NOT LIKE";
        case IS_NULL:     return "IS NULL";
        case IS_NOT_NULL: return "IS NOT NULL";
        default:          return "??";
        }
    }

    std::string valueToString() const {
        if (op == IS_NULL || op == IS_NOT_NULL) {
            return "";
        }
        return visit(
            [](auto&& arg) -> std::string {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, std::string>) {
                    return fmt::format("'{}'", arg);
                } else if constexpr (std::is_same_v<T, std::monostate>) {
                    return "";
                } else {
                    return fmt::format("{}", arg);
                }
            },
            value);
    }

    static bool like_match(const std::string& str, const std::string& pattern) {
        // static cache and mutex
        static std::mutex cache_mutex;
        static auto       regex_cache = std::unordered_map<std::string, std::shared_ptr<RE2>>{};

        std::shared_ptr<RE2> re;

        // check cache
        {
            std::lock_guard<std::mutex> lock(cache_mutex);
            auto                        it = regex_cache.find(pattern);
            if (it != regex_cache.end()) {
                re = it->second;
            }
        }

        // cache miss and compile
        if (!re) {
            // conver to regex
            std::string regex_str;
            for (char c: pattern) {
                if (c == '%') {
                    regex_str += ".*";
                } else if (c == '_') {
                    regex_str += '.';
                } else {
                    // escape sepcical characters
                    if (c == '\\' || c == '.' || c == '^' || c == '$' || c == '|' || c == '?'
                        || c == '*' || c == '+' || c == '(' || c == ')' || c == '[' || c == ']'
                        || c == '{' || c == '}' || c == ' ') {
                        regex_str += '\\';
                    }
                    regex_str += c;
                }
            }

            RE2::Options options;

            auto new_re = std::make_shared<RE2>(regex_str, options);
            if (!new_re->ok()) {
                return false; // invalid regex
            }

            // avoid duplicate insertion
            std::lock_guard<std::mutex> lock(cache_mutex);
            if (auto itr = regex_cache.find(pattern); itr == regex_cache.end()) {
                auto [it, _] = regex_cache.emplace(pattern, new_re);
                re           = it->second;
            } else {
                re = itr->second;
            }
        }

        // execute full match
        return RE2::FullMatch(str, *re);
    }

    static std::optional<double> get_numeric_value(const Data& data) {
        if (auto* i32 = std::get_if<int32_t>(&data)) {
            return *i32;
        } else if (auto* i64 = std::get_if<int64_t>(&data)) {
            return static_cast<double>(*i64);
        } else if (auto* d = std::get_if<double>(&data)) {
            return *d;
        } else {
            return std::nullopt;
        }
    }

    static std::optional<double> get_numeric_value(const Literal& value) {
        if (auto* i = std::get_if<int64_t>(&value)) {
            return *i;
        } else if (auto* d = std::get_if<double>(&value)) {
            return *d;
        } else {
            return std::nullopt;
        }
    }
};

struct LogicalOperation: Statement {
    enum Type {
        AND,
        OR,
        NOT
    };

    Type                                    op_type;
    std::vector<std::unique_ptr<Statement>> children;

    static std::unique_ptr<LogicalOperation> makeAnd(std::unique_ptr<Statement> l,
        std::unique_ptr<Statement>                                              r) {
        auto node     = std::make_unique<LogicalOperation>();
        node->op_type = AND;
        node->children.push_back(std::move(l));
        node->children.push_back(std::move(r));
        return node;
    }

    static std::unique_ptr<LogicalOperation> makeOr(std::unique_ptr<Statement> l,
        std::unique_ptr<Statement>                                             r) {
        auto node     = std::make_unique<LogicalOperation>();
        node->op_type = OR;
        node->children.push_back(std::move(l));
        node->children.push_back(std::move(r));
        return node;
    }

    static std::unique_ptr<LogicalOperation> makeNot(std::unique_ptr<Statement> child) {
        auto node     = std::make_unique<LogicalOperation>();
        node->op_type = NOT;
        node->children.push_back(std::move(child));
        return node;
    }

    std::string pretty_print(int indent) const override {
        std::string op_str = [this] {
            switch (op_type) {
            case AND: return "AND";
            case OR:  return "OR";
            case NOT: return "NOT";
            default:  return "UNKNOWN";
            }
        }();

        std::string result = fmt::format("{:{}}[{}]\n", "", indent, op_str);

        for (auto& child: children) {
            result += child->pretty_print(indent + 2) + "\n";
        }

        if (!children.empty()) {
            result.pop_back();
        }
        return result;
    }

    bool eval(const std::vector<Data>& record) const override;
};
