#include "sqlgen.h"
#include "../stringtools.h"
#include <regex>
#include <iostream>

enum CPPFileTokenType
{
	CPPFileTokenType_Code,
	CPPFileTokenType_Comment
};

struct CPPToken
{
	CPPToken(const std::string& data, CPPFileTokenType type)
		: data(data), type(type)
	{
	}

	std::string data;
	CPPFileTokenType type;
};

enum TokenizeState
{
	TokenizeState_None,
	TokenizeState_CommentMultiline,
	TokenizeState_CommentSingleline
};

std::vector<CPPToken> tokenizeFile(std::string &cppfile)
{
	std::regex find_comments("(/\\*(\\S|\\s)*?\\*/)|(//.*)", std::regex::ECMAScript);
	auto comments_begin=std::regex_iterator<std::string::iterator>(cppfile.begin(), cppfile.end(), find_comments);
	auto comments_end=std::regex_iterator<std::string::iterator>();

	std::vector<CPPToken> tokens;
	size_t lastPos=0;
	for(auto i=comments_begin;i!=comments_end;++i)
	{
		auto m=*i;
		size_t pos=m.position(0);
		if(lastPos<pos)
		{
			tokens.push_back(CPPToken(cppfile.substr(lastPos, pos-lastPos), CPPFileTokenType_Code));
			lastPos=pos;
		}
		tokens.push_back(CPPToken(m.str(), CPPFileTokenType_Comment));
		lastPos+=m.length();
	}
	if(lastPos<cppfile.size())
	{
		tokens.push_back(CPPToken(cppfile.substr(lastPos), CPPFileTokenType_Code));
	}

	return tokens;
}

struct AnnotatedCode
{
	AnnotatedCode(std::map<std::string, std::string> annotations, std::string code)
		: annotations(annotations), code(code)
	{
	}

	AnnotatedCode(std::string code)
		: code(code)
	{
	}

	std::map<std::string, std::string> annotations;
	std::string code;
};

std::string cleanup_annotation(const std::string& annotation)
{
	int state=0;
	std::string ret;
	for(size_t i=0;i<annotation.size();++i)
	{
		if(state==0)
		{
			if(annotation[i]=='\n' || annotation[i]=='\r')
			{
				state=1;
			}
		}
		else if(state==1)
		{
			if(annotation[i]!=' ' && annotation[i]!='*' && annotation[i]!='\n' && annotation[i]!='\t' )
			{
				state=0;
			}
		}

		if(annotation[i]=='\n')
		{
			ret+=" ";
		}

		if(state==0)
		{
			ret+=annotation[i];
		}
	}

	return ret;
};

std::string extractFirstFunction(const std::string& data)
{
	int c=0;
	bool was_in_function=false;
	for(size_t i=0;i<data.size();++i)
	{
		if(data[i]=='{') ++c;
		if(data[i]=='}') --c;

		if(c>0)
		{
			was_in_function=true;
		}

		if(c==0 && was_in_function)
		{
			return data.substr(0, i+1);
		}
	}

	return std::string();
}

std::vector<AnnotatedCode> getAnnotatedCode(const std::vector<CPPToken>& tokens)
{
	std::vector<AnnotatedCode> ret;
	for(size_t i=0;i<tokens.size();++i)
	{
		if(tokens[i].type==CPPFileTokenType_Comment)
		{
			std::map<std::string, std::string> annotations;

			std::regex find_annotations("@([^ \\r\\n]*)[ ]*(((?!@)(?!\\*/)(\\S|\\s))*)", std::regex::ECMAScript);

			for(auto it=std::regex_iterator<std::string::const_iterator>(tokens[i].data.begin(), tokens[i].data.end(), find_annotations);
				it!=std::regex_iterator<std::string::const_iterator>();++it)
			{
				auto m=*it;
				std::string annotation_text=m[2].str();
				annotations[m[1].str()]=trim(cleanup_annotation(annotation_text));
			}

			ret.push_back(AnnotatedCode(tokens[i].data));

			if(!annotations.empty())
			{
				if(i+1<tokens.size() && tokens[i+1].type==CPPFileTokenType_Code)
				{
					std::string next_code=tokens[i+1].data;
					std::string first_function=extractFirstFunction(next_code);

					if(!first_function.empty())
					{
						ret.push_back(AnnotatedCode(annotations, first_function));
						ret.push_back(AnnotatedCode(next_code.substr(first_function.size())));
						++i;
					}
					else
					{
						ret.push_back(AnnotatedCode(annotations, ""));
					}
				}
			}
		}
		else
		{
			ret.push_back(AnnotatedCode(tokens[i].data));
		}
	}

	return ret;
}

struct ReturnType
{
	ReturnType(std::string type, std::string name)
		: type(type), name(name)
	{
	}

	std::string type;
	std::string name;
};

std::vector<ReturnType> parseReturnTypes(std::string return_str)
{
	std::vector<std::string> toks;
	Tokenize(return_str, toks, ",");
	std::vector<ReturnType> ret;
	for(size_t i=0;i<toks.size();++i)
	{
		toks[i]=trim(toks[i]);
		ret.push_back(ReturnType(getuntil(" ", toks[i]), getafter(" ", toks[i])));
	}
	return ret;
}

std::string parseSqlString(std::string sql, std::vector<ReturnType>& types)
{
	std::regex find_var(":([^ (]*)\\(([^)]*?)\\)",std::regex::ECMAScript);
	size_t lastPos=0;
	std::string retSql;
	for(auto it=std::regex_iterator<std::string::const_iterator>(sql.begin(), sql.end(), find_var);
				it!=std::regex_iterator<std::string::const_iterator>();++it)
	{
		auto m=*it;
		if(m.position()>lastPos)
		{
			retSql+=sql.substr(lastPos, m.position()-lastPos);
			lastPos=m.position()+m[0].length();
		}
		retSql+="?";
		types.push_back(ReturnType(m[2].str(), m[1].str()));
	}
	if(lastPos<sql.size())
	{
		retSql+=sql.substr(lastPos);
	}
	return retSql;
}

struct GeneratedData
{
	std::string createQueriesCode;
	std::string destroyQueriesCode;
	std::string funcdecls;
	std::string structures;
	std::string variables;
};

void generateStructure(std::string name, std::vector<ReturnType> return_types, GeneratedData& gen_data, bool use_exists)
{
	if(gen_data.structures.find("struct "+name+"\r\n")!=std::string::npos)
	{
		return;
	}

	gen_data.structures+="\tstruct "+name+"\r\n";
	gen_data.structures+="\t{\r\n";
	if(use_exists)
	{
		gen_data.structures+="\t\tbool exists;\r\n";
	}
	for(size_t i=0;i<return_types.size();++i)
	{
		std::string type=return_types[i].type;
		if(type=="string")
			type="std::wstring";

		gen_data.structures+="\t\t"+type+" "+return_types[i].name+";\r\n";
	}
	gen_data.structures+="\t};\r\n";
}

std::string generateConditional(ReturnType rtype, GeneratedData& gen_data)
{
	std::string cond_name;
	if(!rtype.type.empty())
	{
		cond_name+=(char)toupper(rtype.type[0]);
		cond_name+=rtype.type.substr(1);
	}
	cond_name="Cond"+cond_name;

	if(gen_data.structures.find("struct "+cond_name+"\r\n")!=std::string::npos)
	{
		return cond_name;
	}

	gen_data.structures+="\tstruct "+cond_name+"\r\n";
	gen_data.structures+="\t{\r\n";
	gen_data.structures+="\t\tbool exists;\r\n";
	std::string type=rtype.type;
	if(type=="string")
		type="std::wstring";
	gen_data.structures+="\t\t"+type+" value;\r\n";
	gen_data.structures+="\t};\r\n";

	return cond_name;
}

enum StatementType
{
	StatementType_Select,
	StatementType_Delete,
	StatementType_Insert,
	StatementType_Update,
	StatementType_None
};

AnnotatedCode generateSqlFunction(IDatabase* db, AnnotatedCode input, GeneratedData& gen_data)
{
	std::string sql=input.annotations["sql"];
	std::string func=input.annotations["func"];
	std::string return_type=getuntil(" ", func);
	std::string funcsig=getafter(" ", func);

	std::string struct_name=return_type;

	std::string query_name=funcsig;
	std::string func_s_name=funcsig;
	std::string classname;
	if(query_name.find("::")!=std::string::npos)
	{
		classname=getuntil("::", query_name);
		query_name=getafter("::", query_name);
		func_s_name=query_name;
	}

	query_name="q_"+query_name;

	bool return_vector=false;

	if(return_type.find("vector")==0)
	{
		return_type="std::"+return_type;
	}

	if(return_type.find("std::vector")==0)
	{
		struct_name=getbetween("<", ">", return_type);
		return_vector=true;
	}

	StatementType stmt_type=StatementType_None;
	if(strlower(sql).find("select")!=std::string::npos)
	{
		stmt_type=StatementType_Select;
	}
	if(strlower(sql).find("delete")!=std::string::npos)
	{
		stmt_type=StatementType_Delete;
	}
	if(strlower(sql).find("insert")!=std::string::npos)
	{
		stmt_type=StatementType_Insert;
	}

	std::string return_vals=input.annotations["return"];

	std::vector<ReturnType> return_types=parseReturnTypes(return_vals);

	bool use_struct=false;
	bool use_cond=false;
	bool use_exists=false;
	if(return_types.size()>1)
	{
		use_struct=true;
	}
	
	if(return_vector)
	{
		if(return_types.size()>1)
		{
			generateStructure(struct_name, return_types, gen_data, false);
		}
	}
	else if(strlower(return_type)!="void")
	{
		if(return_types.size()==1)
		{
			use_cond=true;
			use_exists=true;
			struct_name=generateConditional(return_types[0], gen_data);
		}
		else
		{
			generateStructure(struct_name, return_types, gen_data, true);
			use_exists=true;
		}
	}

	std::vector<ReturnType> params;
	std::string parsedSql=parseSqlString(sql, params);

	IQuery *q=db->Prepare("EXPLAIN "+parsedSql, true);

	if(q==NULL)
	{
		std::cout << "ERROR preparing statement: " << parsedSql << std::endl;
		return AnnotatedCode(input.annotations, "");
	}
	else
	{
		q->Read();
	}

	

	std::string return_outer=return_type;
	if(return_vector)
	{
		return_outer="std::vector<";
		if(use_struct)
		{
			return_outer+=(classname.empty()?"":classname+"::")+struct_name;
		}
		else
		{
			return_outer+=return_types[0].type;
		}
		return_outer+=">";
	}
	else if(use_cond)
	{
		return_outer=(classname.empty()?"":classname+"::")+struct_name;
		return_type=struct_name;
	}
	else if(struct_name!="string" && struct_name!="void" && struct_name!="int")
	{
		return_outer=(classname.empty()?"":classname+"::")+struct_name;
		return_type=struct_name;
	}
	

	if(return_outer=="string")
		return_outer="std::wstring";

	if(return_type=="string")
		return_type="std::wstring";

	std::string funcdecl=return_type+" "+func_s_name+"(";
	std::string code="\r\n"+return_outer+" "+funcsig+"(";
	for(size_t i=0;i<params.size();++i)
	{
		bool found=false;
		for(size_t j=0;j<i;++j)
		{
			if(params[j].name==params[i].name)
			{
				found=true;
				break;
			}
		}
		if(found)
		{
			continue;
		}

		if(i>0)
		{
			code+=", ";
			funcdecl+=", ";
		}
		std::string type=params[i].type;
		if(type=="string" || type=="std::wstring" )
		{
			type="const std::wstring&";
		}
		else if(type=="blob")
		{
			type="const std::string&";
		}
		code+=type+" "+params[i].name;
		funcdecl+=type+" "+params[i].name;
	}
	if(params.empty())
	{
		code+="void";
		funcdecl+="void";
	}
	code+=")\r\n{\r\n";
	funcdecl+=");";

	gen_data.funcdecls+="\t"+funcdecl+"\r\n";
	gen_data.variables+="\tIQuery* "+query_name+";\r\n";
	gen_data.createQueriesCode+="\t"+query_name+"="+"db->Prepare(\""+parsedSql+"\", false);\r\n";
	gen_data.destroyQueriesCode+="\tdb->destroyQuery("+query_name+");\r\n";

	for(size_t i=0;i<params.size();++i)
	{
		if(params[i].type=="blob")
		{
			code+="\t"+query_name+"->Bind("+params[i].name+".c_str(), (_u32)"+params[i].name+".size());\r\n";
		}
		else
		{
			code+="\t"+query_name+"->Bind("+params[i].name+");\r\n";
		}
	}

	if(stmt_type==StatementType_Select)
	{
		code+="\tdb_results res="+query_name+"->Read();\r\n";
	}
	else if(stmt_type==StatementType_Delete || stmt_type==StatementType_Insert)
	{
		code+="\t"+query_name+"->Write();\r\n";
	}

	if(!params.empty())
	{
		code+="\t"+query_name+"->Reset();\r\n";
	}

	if(return_vector)
	{
		code+="\tstd::vector<";
		if(use_struct)
		{
			code+=(classname.empty()?"":classname+"::")+struct_name;
		}
		else
		{
			code+=return_types[0].type;
		}
		code+="> ret;\r\n";
		code+="\tret.resize(res.size());\r\n";
		code+="\tfor(size_t i=0;i<res.size();++i)\r\n";
		code+="\t{\r\n";
		if(use_struct)
		{
			code+="\t\t"+struct_name+" tmp;\r\n";
			for(size_t i=0;i<return_types.size();++i)
			{
				if(return_types[i].type=="int")
				{
					code+="\t\ttmp."+return_types[i].name+"=watoi(res[i][L\""+return_types[i].name+"\"]);\r\n";
				}
				else if(return_types[i].type=="int64")
				{
					code+="\t\ttmp."+return_types[i].name+"=watoi64(res[i][L\""+return_types[i].name+"\"]);\r\n";
				}
				else
				{
					code+="\t\ttmp."+return_types[i].name+"=res[i][L\""+return_types[i].name+"\"];\r\n";
				}			
			}
			code+="\t\tret[i]=tmp;\r\n";
		}
		else
		{
			if(return_types[0].type=="int")
			{
				code+="\t\tret[i]=watoi(res[i][L\""+return_types[0].name+"\"]);\r\n";
			}
			else if(return_types[0].type=="int64")
			{
				code+="\t\tret[i]=watoi64(res[i][L\""+return_types[0].name+"\"]);\r\n";
			}
			else
			{
				code+="\t\tret[i]=res[i][L\""+return_types[0].name+"\"]);\r\n";
			}
		}
		code+="\t}\r\n";
		code+="\treturn ret;\r\n";
	}
	else if(!return_types.empty())
	{
		code+="\t"+struct_name+" ret;\r\n";
		code+="\tif(!res.empty())\r\n";
		code+="\t{\r\n";
		if(use_exists)
		{
			code+="\t\tret.exists=true;\r\n";
		}
		if(!use_cond)
		{
			for(size_t i=0;i<return_types.size();++i)
			{
				if(return_types[i].type=="int")
				{
					code+="\t\tret."+return_types[i].name+"=watoi(res[0][L\""+return_types[i].name+"\"]);\r\n";
				}
				else if(return_types[i].type=="int64")
				{
					code+="\t\tret."+return_types[i].name+"=watoi64(res[0][L\""+return_types[i].name+"\"]);\r\n";
				}
				else
				{
					code+="\t\tret."+return_types[i].name+"=res[0][L\""+return_types[i].name+"\"];\r\n";
				}			
			}
		}
		else
		{
			if(return_types[0].type=="int")
			{
				code+="\t\tret.value=watoi(res[0][L\""+return_types[0].name+"\"]);\r\n";
			}
			else if(return_types[0].type=="int64")
			{
				code+="\t\tret.value=watoi64(res[0][L\""+return_types[0].name+"\"]);\r\n";
			}
			else
			{
				code+="\t\tret.value=res[0][L\""+return_types[0].name+"\"];\r\n";
			}
		}
		code+="\t}\r\n";
		code+="\telse\r\n";
		code+="\t{\r\n";
		if(use_exists)
		{
			code+="\t\tret.exists=false;\r\n";
		}
		code+="\t}\r\n";
		code+="\treturn ret;\r\n";			
	}
	code+="}";
	return AnnotatedCode(input.annotations, code);
}

void setup1(IDatabase* db, std::vector<AnnotatedCode>& annotated_code)
{
	for(size_t i=0;i<annotated_code.size();++i)
	{
		AnnotatedCode& curr=annotated_code[i];
		if(!curr.annotations.empty())
		{
			if(curr.annotations.find("-SQLGenTempSetup")!=curr.annotations.end())
			{
				std::map<std::string, std::string>::iterator sql_it=curr.annotations.find("sql");
				if(sql_it!=curr.annotations.end())
				{
					db->Write(sql_it->second);
				}
			}
		}
	}
}

void generateCode1(IDatabase* db, std::vector<AnnotatedCode>& annotated_code, GeneratedData& generated_data)
{
	for(size_t i=0;i<annotated_code.size();++i)
	{
		AnnotatedCode& curr=annotated_code[i];
		if(!curr.annotations.empty())
		{
			if(curr.annotations.find("-SQLGenAccess")!=curr.annotations.end())
			{
				annotated_code[i]=generateSqlFunction(db, curr, generated_data);
			}
		}
	}
}

std::string replaceFunctionContent(std::string new_content, std::string data)
{
	int c=0;
	std::string ret;
	for(size_t i=0;i<data.size();++i)
	{
		if(data[i]=='{')
			++c;
		else if(data[i]=='}')
			--c;

		if(c>0)
		{
			if(!new_content.empty())
				ret+="{";
			ret+=new_content;
			new_content.clear();
		}
		else
		{
			ret+=data[i];
		}
	}
	return ret;
}

std::string generateSetupFunction(std::string data, GeneratedData& generated_data)
{
	return replaceFunctionContent("\r\n"+generated_data.createQueriesCode, data);
}

std::string generateDestructionFunction(std::string data, GeneratedData& generated_data)
{
	return replaceFunctionContent("\r\n"+generated_data.destroyQueriesCode, data);
}

void generateCode2(std::vector<AnnotatedCode>& annotated_code, GeneratedData& generated_data)
{
	for(size_t i=0;i<annotated_code.size();++i)
	{
		AnnotatedCode& curr=annotated_code[i];
		if(!curr.annotations.empty())
		{
			if(curr.annotations.find("-SQLGenSetup")!=curr.annotations.end())
			{
				annotated_code[i]=generateSetupFunction(curr.code, generated_data);
			}
			else if(curr.annotations.find("-SQLGenDestruction")!=curr.annotations.end())
			{
				annotated_code[i]=generateDestructionFunction(curr.code, generated_data);
			}
		}
	}
}

std::string getCode(const std::vector<AnnotatedCode>& annotated_code)
{
	std::string code;
	for(size_t i=0;i<annotated_code.size();++i)
	{
		const AnnotatedCode& curr=annotated_code[i];
		code+=curr.code;
	}
	return code;
}

std::string setbetween(std::string s1, std::string s2, std::string toset, std::string data)
{
	size_t start_pos=data.find(s1);
	if(start_pos==std::string::npos)
		return std::string();

	size_t end_pos=data.find(s2, start_pos);
	if(end_pos==std::string::npos)
		return std::string();

	return data.substr(0, start_pos+s1.size())+"\r\n"+toset+data.substr(end_pos, data.size()-end_pos);
}

std::string placeData(std::string headerfile, GeneratedData generated_data)
{
	std::string t_headerfile=setbetween("//@-SQLGenFunctionsBegin", "//@-SQLGenFunctionsEnd", generated_data.structures+"\r\n\r\n"+generated_data.funcdecls+"\t", headerfile);

	if(t_headerfile.empty())
	{
		std::cout << "ERROR: Cannot find \"//@-SQLGenFunctionsBegin\" or \"//@-SQLGenFunctionsEnd\" in Header-file" << std::endl;
		return headerfile;
	}

	t_headerfile=setbetween("//@-SQLGenVariablesBegin", "//@-SQLGenVariablesEnd", generated_data.variables+"\t", t_headerfile);

	if(t_headerfile.empty())
	{
		std::cout << "ERROR: Cannot find \"//@-SQLGenVariablesBegin\" or \"//@-SQLGenVariablesEnd\" in Header-file" << std::endl;
		return headerfile;
	}

	return t_headerfile;
}

void sqlgen(IDatabase* db, std::string &cppfile, std::string &headerfile)
{
	std::vector<CPPToken> tokens=tokenizeFile(cppfile);
	std::vector<AnnotatedCode> annotated_code=getAnnotatedCode(tokens);
	GeneratedData generated_data;
	setup1(db, annotated_code);
	generateCode1(db, annotated_code, generated_data);
	generateCode2(annotated_code, generated_data);
	cppfile=getCode(annotated_code);
	headerfile=placeData(headerfile, generated_data);
}