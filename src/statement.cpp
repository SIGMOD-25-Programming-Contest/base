#include <plan.h>
#include <statement.h>

bool Comparison::eval(const std::vector<Data>& record) const {
    const Data& record_data = record[column];
    const auto& comp_value  = value;

    switch (op) {
    case IS_NULL:     return std::holds_alternative<std::monostate>(record_data);
    case IS_NOT_NULL: return !std::holds_alternative<std::monostate>(record_data);
    default:          break;
    }

    if (op == LIKE || op == NOT_LIKE) {
        const std::string* record_str = std::get_if<std::string>(&record_data);
        const std::string* comp_str   = std::get_if<std::string>(&comp_value);
        if (!record_str || !comp_str) {
            return false;
        }
        bool match = like_match(*record_str, *comp_str);
        return (op == LIKE) ? match : !match;
    } else {
        auto record_num = get_numeric_value(record_data);
        auto comp_num   = get_numeric_value(comp_value);
        if (record_num.has_value() && comp_num.has_value()) {
            switch (op) {
            case EQ:  return *record_num == *comp_num;
            case NEQ: return *record_num != *comp_num;
            case LT:  return *record_num < *comp_num;
            case GT:  return *record_num > *comp_num;
            case LEQ: return *record_num <= *comp_num;
            case GEQ: return *record_num >= *comp_num;
            default:  return false;
            }
        } else {
            const std::string* record_str = std::get_if<std::string>(&record_data);
            const std::string* comp_str   = std::get_if<std::string>(&comp_value);
            if (record_str && comp_str) {
                switch (op) {
                case EQ:  return *record_str == *comp_str;
                case NEQ: return *record_str != *comp_str;
                case LT:  return *record_str < *comp_str;
                case GT:  return *record_str > *comp_str;
                case LEQ: return *record_str <= *comp_str;
                case GEQ: return *record_str >= *comp_str;
                default:  return false;
                }
            } else {
                return false;
            }
        }
    }
}

bool LogicalOperation::eval(const std::vector<Data>& record) const {
    switch (op_type) {
    case AND: {
        for (const auto& child: children) {
            if (!child->eval(record)) {
                return false;
            }
        }
        return true;
    }
    case OR: {
        for (const auto& child: children) {
            if (child->eval(record)) {
                return true;
            }
        }
        return false;
    }
    case NOT: {
        if (children.size() != 1) {
            return false;
        }
        return !children[0]->eval(record);
    }
    default: return false;
    }
}
