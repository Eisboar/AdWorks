#include "FrontEnd.hpp"
#include <stdexcept>
#include <map>
#include <set>
#include <boost/lexical_cast.hpp>
#include <algorithm>
#include <math.h>
//sql
#include <mysql_connection.h>
#include <cppconn/driver.h>
#include <cppconn/prepared_statement.h>
#include <cppconn/resultset.h>
#include <cppconn/metadata.h>
#include <cppconn/resultset_metadata.h>
#include <cppconn/exception.h>
#include <cppconn/warning.h>
#include <boost/numeric/ublas/matrix_proxy.hpp>
#include <boost/numeric/ublas/io.hpp>

using namespace std;
using namespace boost;

FrontEnd::FrontEnd(std::ifstream& in):backEnd_(NULL) {
  parseConfig(in);
}

int FrontEnd::countSameNeighbors(boost::numeric::ublas::matrix<double> a, int i, int j){
	int count=0;
	for (unsigned h = 0; h < a.size1(); ++h)
		if(a(i,h)!=0&&a(j,h)!=0)
			++count;
	return count;
}

double FrontEnd::weight(boost::numeric::ublas::matrix_column<boost::numeric::ublas::matrix<double> > col, int j){
	double sum=0;	
	for (unsigned i = 0; i < col.size(); ++i){
			sum+=col(i);		
		}
	return col(j)/sum;
}

double FrontEnd::spread(boost::numeric::ublas::matrix_row<boost::numeric::ublas::matrix<double> > row){
	int count=0;
	double sum=0;
	for (unsigned i = 0; i < row.size(); ++i)
		if(row(i)!=0){
			count++;
			sum+=row(i);		
		}
	double avg=sum/count;
	sum=0;
	for (unsigned i = 0; i < row.size(); ++i)
		if(row(i)!=0){
			sum+=(avg-row(i))*(avg-row(i));		
		}
	return exp((double)sum/count*-1.0);
	//if(count==0||sum==0) return 0;
	//return pow((sum/count),-1.0);
	
}

bool FrontEnd::tablesExist() {
  try{
    scoped_ptr<sql::PreparedStatement> pstmt;
    scoped_ptr<sql::ResultSet> rs;

    pstmt.reset(con->prepareStatement("SELECT table_name "
				      "FROM information_schema.tables "
				      "WHERE table_schema = 'AdWorks' "
				      "AND table_name = 'QueryRewrites';"));
    //you won't see it
    rs.reset(pstmt->executeQuery());
    return rs->next();
  } catch (sql::SQLException &e) {
    cout<<"in BackEnd::tablesExist: " <<e.getErrorCode()<<endl;
    throw e;
  }
} 
QueryResult FrontEnd::matchAd(std::string query,   
			      const  IUser* user, 
			      bool* foundAd) {
  std::list<std::string> rewrites;

  //rewrite the query and put all rewrites in here
  rewrites.push_back(query);
  
  //add_rewrites 
	scoped_ptr<sql::PreparedStatement> pstmt;
	scoped_ptr<sql::ResultSet> rs;	
	
	try {
		pstmt.reset(con->prepareStatement("SELECT 1_Query, 2_Query, 3_Query, 4_Query, 5_Query FROM QueryRewrites WHERE Query LIKE ?"));
      		pstmt->setString(1, query);
		rs.reset(pstmt->executeQuery());

		if(rs->next()){
			for(unsigned int i=1;i<6;i++){
				string rewrite=rs->getString(i);
				if(rewrite.compare("") != 0)
					rewrites.push_back(rewrite);
				}
		}
	} catch(sql::SQLException &e) {
    		cout << "in BackEnd::matchAdRewrites: " << e.getErrorCode() << endl;
   		 throw e;
  	} 
	/*
	//testoutput
	std::list<std::string>::iterator it;
	for(it=rewrites.begin();it!=rewrites.end();it++)
		cout<<(*it)<<endl;
	*/
  	if(backEnd_ != NULL)
    		return backEnd_->matchAdRewrites(rewrites, user, foundAd);
 	else
    		throw std::runtime_error("backEnd is NULL");
}

std::string FrontEnd::getAdURL(uint32_t adID) {
  if(backEnd_ != NULL)
    return backEnd_->getAdURL(adID);
  else
    throw std::runtime_error("backEnd is NULL");
}

boost::numeric::ublas::vector<int> FrontEnd::getVec(int i, int size){
	//boost::numeric::ublas::vector<int> v=boost::numeric::ublas::zero_vector<int>(size);
	//v(i)=1;
	//return v;
	boost::numeric::ublas::vector<double> v(size);
	for(int j =0; j<size;j++)
		if(i!=j)	
			v(j)=0;
		else
			v(j)=1;
	return v;
}

void FrontEnd::parseConfig(std::ifstream& in) {
  if (!in.is_open()) throw std::runtime_error("File not found");

  typedef tokenizer< char_separator<char> > Tokenizer;
  char_separator<char> sep("=");
  vector< string > vec;
  string line;
  size_t commentId;
  map<string,string> dbConfig;
  
  while (getline(in,line)){
    commentId=line.find("##");
    if (commentId==string::npos){       
      Tokenizer tok(line,sep);
      //unsafe - wrong syntax like 'newlines' will crash
      Tokenizer::iterator it=tok.begin();
      dbConfig.insert(std::pair<string, string>(*(it++), *(it)));
    }
  }
  
  sql::Driver* driver = get_driver_instance();
  con.reset(driver->connect(dbConfig["Server"]+":"+dbConfig["Port"], dbConfig["User"], dbConfig["Password"]));
  con->setSchema("AdWorks");
}

//Operator for ranking rewrites
struct pairCompareOperator {
  bool operator() (pair<string,double> i,pair<string,double> j) { return(i.second>j.second); }
} pairCompare;


bool FrontEnd::analyzeClickGraph(const std::string& file) { 
	
	
	//parse graphfile
	typedef tokenizer< escaped_list_separator<char> > Tokenizer;
 	escaped_list_separator<char> sep("\\", " \t", "\"");  
	
	ifstream in(file.c_str());
	if (!in.is_open()) throw;
	string line, query, ad;	
	int numClicks;
	std::vector<string> queries;
	std::vector<string> ads;
	std::vector<EDGE> edges;
		
	int maxClicks=0;

	while (getline(in,line)){
		if (line.find("#")!=string::npos) continue;		
		Tokenizer tok(line,sep);
		Tokenizer::iterator tok_iter=tok.begin();
		query=*(tok_iter++);
		queries.push_back(query);
		numClicks=lexical_cast<int>(*(tok_iter++));
		ad=*(tok_iter);
		ads.push_back(ad);
		EDGE edge;
		edge.query=query;
		edge.ad=ad;
		edge.numClicks=numClicks;
		if (numClicks>maxClicks) maxClicks=numClicks;		
		edges.push_back(edge);
		
	}
	//remove duplicates
	std::sort( ads.begin(), ads.end() );
	std::sort( queries.begin(), queries.end() );
   	std::vector<std::string>::iterator new_end_pos;
	new_end_pos = std::unique( ads.begin(), ads.end() );
	ads.erase( new_end_pos, ads.end() );	
	new_end_pos = std::unique( queries.begin(), queries.end() );
   	queries.erase( new_end_pos, queries.end() );
	
	vector<string>::iterator it;
	vector<EDGE>::iterator edgeIt;
	//testoutput	
	/*
	for(it=queries.begin();it!=queries.end();++it)
		cout<<*(it)<<endl;
	for(it=ads.begin();it!=ads.end();++it)
		cout<<*(it)<<endl;
	for(edgeIt=edges.begin();edgeIt!=edges.end();++edgeIt)
	{
		std::cout<<(*edgeIt).query<<" "<<(*edgeIt).ad<<" "<<(*edgeIt).numClicks<<endl;			
	}	
	*/	
	
	//simrank
	//build adjacency-matrix
	boost::numeric::ublas::matrix<double> a=boost::numeric::ublas::zero_matrix<double>(ads.size()+queries.size(),ads.size()+queries.size());
	

	int posInQueries, posInAds;
	for(edgeIt=edges.begin();edgeIt!=edges.end();++edgeIt){
		posInQueries=std::find(queries.begin(), queries.end(),(*edgeIt).query)-queries.begin();
		posInAds=std::find(ads.begin(), ads.end(),(*edgeIt).ad)-ads.begin();
		a(posInQueries,posInAds+queries.size())=((double)(*edgeIt).numClicks)/maxClicks;
		a(posInAds+queries.size(),posInQueries)=((double)(*edgeIt).numClicks)/maxClicks;
	}
	//testputput
	
	for(unsigned int i=0; i<a.size1();i++){
		for(unsigned int j=0; j<a.size2(); j++){
			cout<<a(j,i)<<"\t";
		}
		cout<<std::endl;
	}
	cout<<std::endl;
	//built transition Matrix p
	boost::numeric::ublas::matrix<double> p=boost::numeric::ublas::zero_matrix<double>(a.size1(),a.size1());
	for(unsigned int i=0; i<queries.size();i++){
		for(unsigned int j=queries.size(); j<p.size2(); j++){
			if(a(j,i)==0) continue;
			boost::numeric::ublas::matrix_row<boost::numeric::ublas::matrix<double> > row (a, j);
			boost::numeric::ublas::matrix_column<boost::numeric::ublas::matrix<double> > col (a, i);			
			p(j,i)=spread(row)*weight(col,j);			
		}
		
	}	
	/*
	for(unsigned int i=queries.size(); i<p.size2();i++){
		for(unsigned int j=0; j<queries.size(); j++){
			if(a(j,i)==0) continue;
			boost::numeric::ublas::matrix_row<boost::numeric::ublas::matrix<double> > row (a, j);
			boost::numeric::ublas::matrix_column<boost::numeric::ublas::matrix<double> > col (a, i);			
			p(j,i)=spread(row)*weight(col,j);			
		}
		
	}
	*/
	for(unsigned int i=0; i<a.size1();i++){
		boost::numeric::ublas::matrix_column<boost::numeric::ublas::matrix<double> > col (p, i);	
		double sum=0;	
		for (unsigned j = 0; j < col.size(); ++j){
			sum+=col(j);		
		}
		p(i,i)=1.0-sum;
	}
	
	cout<<std::endl;
	for(unsigned int i=0; i<p.size1();i++){
		for(unsigned int j=0; j<p.size2(); j++){
			cout<<p(i,j)<<"\t";
		}
		cout<<std::endl;
	}
	
	

	//bulit identity-matrix
	boost::numeric::ublas::matrix<double> id=boost::numeric::ublas::zero_matrix<int>(ads.size()+queries.size(),ads.size()+queries.size());
	for(unsigned int i=0; i<p.size1();i++)
		id(i,i)=1;
		
	//bulid V
	boost::numeric::ublas::matrix<double> v=boost::numeric::ublas::zero_matrix<int>(ads.size()+queries.size(),ads.size()+queries.size());
	for(unsigned int i=0; i<a.size1();i++){
		for(unsigned int j=0; j<a.size2(); j++){
			double sum=0;
			for(int h=1;h<=countSameNeighbors(a,i,j);++h)
				sum+=1.0/(pow(2,h));			
			if(sum==0) v(i,j)=0.25;			
			v(i,j)=sum;
			
		}	
	}

	cout<<std::endl;
	for(unsigned int i=0; i<v.size1();i++){
		for(unsigned int j=0; j<v.size2(); j++){
			cout<<v(i,j)<<"\t";
		}
		cout<<std::endl;
	}	

	boost::numeric::ublas::matrix<double> s=id;
	int k=5;
	double c=0.8;
	//calc simrank with C=0.8, k=20
	for(int loop=0;loop<k;loop++){
	s=prod(c*trans(p),s);
		
	s=prod(s,p);
	//set diag=1	
	for(unsigned int col=0; col<s.size1();col++)
		for(unsigned int row=0; row<s.size2();row++)
			if(row==col)
				s(col,row)=1;	
	}
	for(unsigned int i=0; i<s.size1();i++)
	for(unsigned int j=0; j<s.size2(); j++)		
		s(i,j)=s(i,j)*v(i,j);
	
	//s=prod(v,s);
	for(unsigned int i=0; i<s.size1();i++){
	for(unsigned int j=0; j<s.size2(); j++){
		cout<<s(i,j)<<"\t";
	}
	cout<<std::endl;
	}
	
	

	//rank-rewirtes
	std::vector<pair<string,std::vector<pair<string,double> > > > rankedRewrites;
	for(unsigned int i=0; i<queries.size();++i){
		std::vector<pair<string,double> > simQuery;
		for(unsigned int j=0;j<queries.size();++j){			
			if(i==j||s(i,j)==0) continue;
			simQuery.push_back(pair<string,double>(queries[j],s(i,j)));			
		}
		std::sort(simQuery.begin(),simQuery.end(),pairCompare);		
		rankedRewrites.push_back(pair<string,std::vector<pair<string,double> > >(queries[i],simQuery));
	}	
	std::vector<pair<string,std::vector<pair<string,double> > > >::iterator rankedRewritesIt;
	std::vector<pair<string,double> >::iterator simQueryIt;
	//Testoutput	
	for(rankedRewritesIt=rankedRewrites.begin();rankedRewritesIt!=rankedRewrites.end();++rankedRewritesIt){
		cout<<(*rankedRewritesIt).first<<":\t";	
		for(simQueryIt=(*rankedRewritesIt).second.begin();simQueryIt!=(*rankedRewritesIt).second.end();++simQueryIt){
			cout<<(*simQueryIt).first<<"\t"<<(*simQueryIt).second<<"-\t";				
		}		
		cout<<endl;
	}
	
	
	
	//load into DB
	boost::scoped_ptr<sql::PreparedStatement> pstmt;
	if(tablesExist()) {
   	 	//drop all
       		try {
     			pstmt.reset(con->prepareStatement("DROP TABLE QueryRewrites"));
     			pstmt->executeUpdate();
    		} catch(sql::SQLException &e) {
      			cout<<"in FrontEnd::initDatabase whiledropping: " <<e.getErrorCode()<<endl;
      			throw e;
   		} 
  	}
	 //create all
 	 try {
    		pstmt.reset(con->prepareStatement("CREATE TABLE QueryRewrites(Query VARCHAR(60) PRIMARY KEY, 1_Query VARCHAR(60), 1_Score DOUBLE, 2_Query VARCHAR(60), 2_Score DOUBLE, 3_Query VARCHAR(60), 3_Score DOUBLE, 4_Query VARCHAR(60), 4_Score DOUBLE, 5_Query VARCHAR(60), 5_Score DOUBLE)"));
    		pstmt->executeUpdate();
  	} catch(sql::SQLException &e) {
    		cout<<"in FrontEnd::initDatabase while creating: " << e.getErrorCode() << endl;
    		throw e;
 	}
	

	try{
     		pstmt.reset(con->prepareStatement("INSERT INTO QueryRewrites(Query, 1_Query, 1_Score, 2_Query, 2_Score, 3_Query, 3_Score, 4_Query, 4_Score, 5_Query, 5_Score) VALUES (?, ?, ?, ?, ? ,?, ? ,?,?,?,?)"));
     		for(rankedRewritesIt=rankedRewrites.begin();rankedRewritesIt!=rankedRewrites.end();++rankedRewritesIt){			
			pstmt->setString(1, (*rankedRewritesIt).first);
			int count=2;	
			for(simQueryIt=(*rankedRewritesIt).second.begin();simQueryIt!=(*rankedRewritesIt).second.end();++simQueryIt){
				pstmt->setString(count, (*simQueryIt).first);
				pstmt->setDouble(count+1, (*simQueryIt).second);
				count+=2;				
				if(count>10) break;
			}
			for(count=count;count<11;count+=2){
				pstmt->setString(count, "");
				pstmt->setDouble(count+1, 0);			
			}
			pstmt->executeUpdate();
		}	
        }catch(sql::SQLException &e) {
     		 cout << "in BackEnd::initDatabase while inserting ads: " << e.getErrorCode() << endl;
      		throw e;
    	}
	
	(void)file; return true; 
}

bool FrontEnd::analyzeDemographicFeatures(const std::string& userFile, 
				const std::string& visitFile)
{ (void)userFile; (void)visitFile; return true;}

void FrontEnd::setBackend(IBackEnd* backend) {
  this->backEnd_ = backend;
}
