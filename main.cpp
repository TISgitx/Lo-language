// main.cpp
#include <iostream>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <string>
#include <vector>
#include <stack>
#include <map>
#include <regex>
#include "src/h/variable.h"
#include "src/h/function.h"
#include "src/h/utils.h"
#include "src/h/evaluator.h"
#include "src/h/executor.h"

struct Context {
    std::map<std::string, FunctionDef> functions;
    std::unordered_map<std::string, Variable> variables;
};

struct IfState {
    bool matched;
    bool skipping; // true — we skip body
};

static std::regex locRegex(R"(^loc\s+(\w+)\s*=\s*(int|str|bool|arr)\((.*)\)\s*!$)");
static std::regex assignRegex(R"(^(\w+)\s*=\s*(.+)\!$)");
static std::regex inputRegex(R"(^(\w+)\s*=\s*input--\s*(i|str)-\s*\"([^\"]*)\"!$)");
static std::regex funRegex(R"(^funS\s+(\w+)\s+(\w+)\(([^)]*)\):\s*\{$)");
static std::regex returnRegex(R"(^return\s+(.*)!$)");
static std::regex printRegex(R"(^print--\s*(?:(\"([^\"]*)\")|(\w+)|f-(\w+)\(([^)]*)\))!$)");
// groups: 2 = literal text, 3 = variable, 4 = func name, 5 = func args

void errorAndExit(int lineno, const std::string &msg) {
    std::cerr << "Error at line " << lineno << ": " << msg << std::endl;
    exit(1);
}

bool startsWith(const std::string &s, const std::string &p) {
    return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}

void processLoc(Context &ctx, const std::smatch &m, int lineno) {
    std::string name = m[1];
    std::string type = m[2];
    std::string raw = trim(m[3]);
    if (type == "str") {
        if (raw.size() >= 2 && raw.front() == '"' && raw.back() == '"') {
            raw = raw.substr(1, raw.size() - 2);
        }
        ctx.variables[name] = {"str", raw};
    } else if (type == "int") {
        std::string val = evalExpression(raw); // we assume that evalExpression returns a string representation of int
        ctx.variables[name] = {"int", val};
    } else if (type == "bool") {
        std::string val = trim(raw);
        if (val == "true" || val == "1") ctx.variables[name] = {"bool", "true"};
        else if (val == "false" || val == "0") ctx.variables[name] = {"bool", "false"};
        else errorAndExit(lineno, "Invalid bool value: " + val);
    } else if (type == "arr") {
        std::string rawList = trim(raw);
        std::vector<std::string> elements;
        std::stringstream ss(rawList);
        std::string item;
        while (std::getline(ss, item, ',')) {
            item = trim(item);
            if (item.size() >= 2 && item.front() == '"' && item.back() == '"')
                item = item.substr(1, item.size() - 2);
            elements.push_back(item);
        }
        std::ostringstream os;
        for (size_t i = 0; i < elements.size(); ++i) {
            if (i) os << ",";
            os << elements[i];
        }
        ctx.variables[name] = {"arr", os.str()};
        
    } else {
        errorAndExit(lineno, "Unknown type for loc: " + type);
    }
}

void processAssign(Context &ctx, const std::smatch &m, int lineno) {
    std::string name = m[1];
    if (!ctx.variables.count(name)) errorAndExit(lineno, "Undefined variable: " + name);
    std::string rhs = trim(m[2]);
    auto &var = ctx.variables[name];
    if (var.type == "int") var.value = evalExpression(rhs);
    else if (var.type == "bool") {
        rhs = trim(rhs);
        if (rhs == "true" || rhs == "1") var.value = "true";
        else if (rhs == "false" || rhs == "0") var.value = "false";
        else errorAndExit(lineno, "Invalid bool assignment: " + rhs);
    } else {
        if (rhs.size() >= 2 && rhs.front() == '"' && rhs.back() == '"') rhs = rhs.substr(1, rhs.size() - 2);
        var.value = rhs;
    }
}

void processInput(Context &ctx, const std::smatch &m, int lineno) {
    std::string name = m[1], type = m[2], prompt = m[3];
    std::cout << prompt;
    std::string input;
    std::getline(std::cin, input);
    if (type == "i") {
        try { std::stoll(input); ctx.variables[name] = {"int", input}; }
        catch (...) { errorAndExit(lineno, "Invalid input for int: " + input); }
    } else ctx.variables[name] = {"str", input};
}

void processPrint(Context &ctx, const std::smatch &m, int lineno) {
    if (m[2].matched) {
        // literal
        std::cout << m[2].str() << std::endl;
    } else if (m[3].matched) {
        // variable
        std::string var = m[3];
        if (!ctx.variables.count(var)) { std::cerr << "Undefined variable: " << var << std::endl; return; }
        auto &v = ctx.variables[var];
        if (v.type == "arr") {
            std::stringstream ss(v.value);
            std::string item;
            std::vector<std::string> vals;
            while (std::getline(ss, item, ',')) vals.push_back(trim(item));
            std::cout << "[";
            for (size_t i = 0; i < vals.size(); ++i) {
                std::cout << vals[i];
                if (i != vals.size() - 1) std::cout << ", ";
            }
            std::cout << "]" << std::endl;
        } else {
            std::cout << v.value << std::endl;
        }
    } else if (m[4].matched) {
        std::string fname = m[4];
        std::string argsStr = m[5];
        std::vector<std::string> args;
        std::stringstream ss(argsStr);
        std::string a;
        while (std::getline(ss, a, ',')) args.push_back(trim(a));
        if (!ctx.functions.count(fname)) errorAndExit(lineno, "Undefined function: " + fname);
        std::string res = executeFunction(ctx.functions[fname], args, ctx.functions, ctx.variables);
        std::cout << res << std::endl;
    } else errorAndExit(lineno, "Bad print expression");
}

int main(int argc, char* argv[]) {
    if (argc < 2) { std::cerr << "Usage: lomake <file.lo>\n"; return 1; }
    std::ifstream file(argv[1]);
    if (!file) { std::cerr << "Failed to open file\n"; return 1; }
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(file, line)) lines.push_back(trim(line));

    Context ctx;
    bool inFunction = false;
    FunctionDef currentFunc;
    std::string currentFuncName;
    std::stack<IfState> ifStack;

    for (size_t i = 0; i < lines.size(); ++i) {
        const std::string &ln = lines[i];
        if (ln.empty()) continue;
        std::smatch match;

        if (inFunction) {
            if (ln == "}") {
                ctx.functions[currentFuncName] = currentFunc;
                inFunction = false;
            } else {
                currentFunc.body.push_back(ln);
            }
            continue;
        }

        if (std::regex_match(ln, match, funRegex)) {
            inFunction = true;
            currentFuncName = match[2];
            currentFunc.returnType = match[1];
            currentFunc.body.clear();
            currentFunc.params.clear();
            std::string paramStr = match[3];
            std::stringstream ss(paramStr);
            std::string p;
            while (std::getline(ss, p, ',')) {
                p = trim(p);
                if (p.empty()) continue;
                size_t colon = p.find(':');
                if (colon != std::string::npos) {
                    std::string type = trim(p.substr(0, colon));
                    std::string name = trim(p.substr(colon + 1));
                    currentFunc.params.emplace_back(type, name);
                } else {
                    // If the parameter has no type, you can decide by default or fail
                    currentFunc.params.emplace_back(std::string("var"), trim(p));
                }
            }
            continue;
        }

        // if / elif / end handling
        if (startsWith(ln, "if-")) {
            // simple parse: if- a >> b the
            std::smatch m2;
            std::regex ifRe(R"(if-\s*(\w+)\s*(>>|<<|===)\s*(\w+)\s*the)");
            if (std::regex_match(ln, m2, ifRe)) {
                bool res = evaluateCondition(ctx.variables, m2[1], m2[2], m2[3]);
                ifStack.push({res, !res});
            } else errorAndExit(i+1, "Malformed if condition");
            continue;
        } else if (startsWith(ln, "elif-")) {
            if (ifStack.empty()) errorAndExit(i+1, "elif without if");
            IfState top = ifStack.top(); ifStack.pop();
            if (top.matched) {
                // earlier branch matched — remain skipping
                ifStack.push({true, true});
            } else {
                std::smatch m2;
                std::regex elifRe(R"(elif-\s*(\w+)\s*(>>|<<|===)\s*(\w+)\s*the)");
                if (!std::regex_match(ln, m2, elifRe)) errorAndExit(i+1, "Malformed elif");
                bool res = evaluateCondition(ctx.variables, m2[1], m2[2], m2[3]);
                ifStack.push({res, !res});
            }
            continue;
        } else if (ln == "end--") {
            if (ifStack.empty()) errorAndExit(i+1, "end-- without if");
            ifStack.pop();
            continue;
        }

        // if we're inside a skipping if body -> ignore line
        if (!ifStack.empty() && ifStack.top().skipping) continue;

        // simple constructs
        if (std::regex_match(ln, match, locRegex)) {
            processLoc(ctx, match, i+1);
        } else if (std::regex_match(ln, match, inputRegex)) {
            processInput(ctx, match, i+1);
        } else if (std::regex_match(ln, match, assignRegex)) {
            processAssign(ctx, match, i+1);
        } else if (std::regex_match(ln, match, printRegex)) {
            processPrint(ctx, match, i+1);
        } else {
            std::cerr << "Syntax error at line " << i+1 << ": " << ln << std::endl;
            return 1;
        }
    }

    return 0;
}