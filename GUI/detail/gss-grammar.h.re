#include <memory>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <iostream>
using std::unique_ptr;

#include <string>
#include <stdexcept>
#include <iostream>


namespace OmfgGUI{
template<class T>
struct TGrammar {
#define self (static_cast<T *>(this))
~TGrammar() { free(buffer); }
struct Token{ typedef std::unique_ptr<Token> ptr;

virtual ~Token() {}
};

struct STRING : public Token {
typedef std::unique_ptr<STRING> ptr;
#define CONSTRUCT(b_, e_) STRING(T& g, char const* b_, char const* e_)

	CONSTRUCT(b, e) : str(b, e) { }

	std::string str;

#undef CONSTRUCT
};
void next() {
#define YYCTYPE char
#define YYCURSOR curp
#define YYLIMIT limit
#define YYMARKER marker
#define YYFILL(n) fill(n)
retry:
begin = curp;
goto append; append:
/*!re2c
any =  [\000-\377] ;
*/
switch(state) {case 0:
/*!re2c
"." {  cur = 1; return; }
"#" {  cur = 2; return; }
":" {  cur = 3; return; }
";" {  cur = 4; return; }
"{" {  cur = 5; return; }
"}" {  cur = 6; return; }
"," {  cur = 7; return; }
">" {  cur = 8; return; }
[a-zA-Z0-9._\\\-%()/]* {  cur = 9; curData.reset(new STRING(*self, begin, YYCURSOR)); return; }
[\ \r\t] { 
#define linecounter() (++this->line)
#define skip() goto retry
#define append() goto append
#define setstate(x_) (state = (x_))
 skip(); 
#undef setstate
#undef append
#undef skip
#undef linecounter
 cur = 10; return; }
"//"[^\r\n\000]* { 
#define linecounter() (++this->line)
#define skip() goto retry
#define append() goto append
#define setstate(x_) (state = (x_))
 skip(); 
#undef setstate
#undef append
#undef skip
#undef linecounter
 cur = 11; return; }
"\n" { 
#define linecounter() (++this->line)
#define skip() goto retry
#define append() goto append
#define setstate(x_) (state = (x_))
 linecounter(); skip(); 
#undef setstate
#undef append
#undef skip
#undef linecounter
 cur = 12; return; }
[\"][^\"]*[\"] { 
#define b (begin)
#define e (YYCURSOR)
 cur = 9; curData.reset(new STRING(*self, b + 1, e - 1)); return; 

#undef b
#undef e
}
"\000" { cur = 0; return; }
[\000-\377] { semanticError(std::string("Unknown character '") + *begin + "'"); goto retry; }
*/
break;
}
#undef YYCTYPE
#undef YYCURSOR
#undef YYLIMIT
#undef YYMARKER
#undef YYFILL
}
void fill(size_t s) {
size_t l = limit - begin;
if(buffer)
	memmove(buffer, begin, l);
size_t newSize = std::max(static_cast<size_t>(1024), l + s);
buffer = (char *)realloc(buffer, newSize);
size_t toRead = newSize - l;
size_t amountRead = self->read(&buffer[l], toRead);
if(amountRead == 0) { memset(&buffer[l], 0, toRead); }
else newSize = l+amountRead;
if(begin) {
ptrdiff_t offs = buffer - begin;
curp += offs;
marker += offs;
} else {
curp = buffer;
marker = buffer;
}
begin = buffer;
limit = buffer + newSize;
}
struct ParsingAborted : public std::exception { };
void fatalError(std::string const& msg = "Syntax error") {
self->reportError(msg, self->getLoc());
throw ParsingAborted();
}
void syntaxError(std::string const& msg = "Syntax error") {
syntaxError(msg, self->getLoc());
}
void syntaxError(std::string const& msg, Location loc) {
self->reportError(msg, loc);
syncTokens = true;
error = true;
}
void semanticError(std::string const& msg = "Semantic error") {
semanticError(msg, self->getLoc());
}
void semanticError(std::string const& msg, Location loc) {
self->reportError(msg, loc);
error = true;
}
void semanticWarning(std::string const& msg = "Semantic warning") {
semanticWarning(msg, self->getLoc());
}
void semanticWarning(std::string const& msg, Location loc) {
self->reportError("warning: " + msg, loc);
}
bool matchToken(int token) {
if(syncTokens) {
while(cur != token && cur != 0) next();
if(cur == 0 && token != 0) fatalError("Unexpected end of file");
syncTokens = false;} else {
if(cur != token) {
syntaxError("Unexpected token");
return false;
 }}
return true; }
bool optionalMatch(int token) {
if(syncTokens) {
while(cur != token && cur != 0) next();
syncTokens = false;if(cur != token) return false;
} else {
if(cur != token) {
return false;
 }}
return true; }
bool full() { return cur == 0 && !error; }
void rule_clause() {
 Context::GSSselector& sel = self->addSelector(); 
while(set_1[cur]) {
if((cur == 1)) {
next();
if(!matchToken(9)) return;
std::unique_ptr<STRING> class_(static_cast<STRING*>(curData.release()));
next();
 sel.addClass(class_->str); 
}
else if((cur == 2)) {
next();
if(!matchToken(9)) return;
std::unique_ptr<STRING> id(static_cast<STRING*>(curData.release()));
next();
 sel.addID(id->str); 
}
else if((cur == 3)) {
next();
if(!matchToken(9)) return;
std::unique_ptr<STRING> state(static_cast<STRING*>(curData.release()));
next();
 sel.addState(state->str); 
}
else if((cur == 8)) {
next();
if(!matchToken(9)) return;
std::unique_ptr<STRING> group(static_cast<STRING*>(curData.release()));
next();
 sel.addGroup(group->str); 
}
else if((cur == 9)) {
std::unique_ptr<STRING> tag(static_cast<STRING*>(curData.release()));
next();
 sel.addTag(tag->str); 
}
else { syntaxError(); return; }
}
if(!matchToken(5)) return;
next();
rule_property(sel);
while((cur == 4)) {
next();
if((cur == 9)) {
rule_property(sel);
}
}
if(!matchToken(6)) return;
next();
}
void rule_document() {
while(set_1[cur]) {
rule_clause();
}
}
void rule_property(Context::GSSselector& sel) {
std::unique_ptr<STRING> name(static_cast<STRING*>(curData.release()));
next();
if(!matchToken(3)) return;
next();
 std::list<std::string>& v = sel.addProperty(name->str); 
while(set_8[cur]) {
if((cur == 9)) {
std::unique_ptr<STRING> value(static_cast<STRING*>(curData.release()));
next();
 v.push_back(value->str); 
}
else if((cur == 2)) {
next();
if(!matchToken(9)) return;
std::unique_ptr<STRING> value(static_cast<STRING*>(curData.release()));
next();
 v.push_back('#' + value->str); 
}
else { syntaxError(); return; }
}
}
bool set_1[14];
bool set_8[14];
TGrammar() : cur(-1), curp(0), limit(0), marker(0), begin(0), buffer(0), line(1), state(0), syncTokens(false), error(false) {
memset(set_1, 0, sizeof(bool)*14);
set_1[1] = true;
set_1[2] = true;
set_1[3] = true;
set_1[8] = true;
set_1[9] = true;
memset(set_8, 0, sizeof(bool)*14);
set_8[2] = true;
set_8[9] = true;
}
int cur;
std::unique_ptr<Token> curData;
char* curp;
char* limit;
char* marker;
char* begin;
char* buffer;
int line;
int state;
bool syncTokens;
bool error;
#undef self
};
}
