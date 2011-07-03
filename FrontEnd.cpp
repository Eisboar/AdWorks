#include "FrontEnd.hpp"
#include <stdexcept>
#include <map>
#include <set>
#include <algorithm>
#include <cmath>
#include <vector>
#include <string>
#include <set>

//sql
#include <mysql_connection.h>
#include <cppconn/driver.h>
#include <cppconn/prepared_statement.h>
#include <cppconn/resultset.h>
#include <cppconn/metadata.h>
#include <cppconn/resultset_metadata.h>
#include <cppconn/exception.h>
#include <cppconn/warning.h>

//boost
#include <boost/numeric/ublas/matrix_proxy.hpp>
#include <boost/numeric/ublas/io.hpp>
#include <boost/tokenizer.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/filesystem.hpp>


namespace ublas = boost::numeric::ublas;
using namespace std;
using namespace boost;

int FrontEnd::countSameNeighbors(ublas::matrix<double> a, int i, int j){
  int count=0;
  for (unsigned h = 0; h < a.size1(); ++h)
    if(a(i,h)!=0&&a(j,h)!=0)
      ++count;
  return count;
}

double FrontEnd::weight(ublas::matrix_column<ublas::matrix<double> > col, int j){
  double sum=0;	
  for (unsigned i = 0; i < col.size(); ++i){
    sum+=col(i);		
  }
  return col(j)/sum;
}

double FrontEnd::spread(ublas::matrix_row<ublas::matrix<double> > row){
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

std::vector<std::string> FrontEnd::matchAd(const std::string& query,   
					   const  IUser*, 
					   bool*) {
  std::vector<std::string> rewrites;

  //rewrite the query and put all rewrites in here
  rewrites.push_back(query);
  
  //add_rewrites 
  scoped_ptr<sql::PreparedStatement> pstmt;
  scoped_ptr<sql::ResultSet> rs;	
	
  try {
    pstmt.reset(con->prepareStatement("SELECT Therm2 " 
				      "FROM Simularity WHERE Therm1 LIKE ?"));
    pstmt->setString(1, query);
    rs.reset(pstmt->executeQuery());

    while(rs->next()){
	string rewrite=rs->getString(1);
        rewrites.push_back(rewrite);
	if(rewrites.size()>5) break;
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

  return rewrites;
}

std::string FrontEnd::getAdURL(uint32_t adID) {
  if(backEnd_ != NULL)
    return backEnd_->getAdURL(adID);
  else
    throw std::runtime_error("backEnd is NULL");
}

ublas::vector<int> FrontEnd::getVec(int i, int size){
  //ublas::vector<int> v=ublas::zero_vector<int>(size);
  //v(i)=1;
  //return v;
  ublas::vector<double> v(size);
  for(int j =0; j<size;j++)
    if(i!=j)	
      v(j)=0;
    else
      v(j)=1;
  return v;
}

//Operator for ranking rewrites
struct pairCompareOperator {
  bool operator() (pair<string,double> i,pair<string,double> j) { return(i.second>j.second); }
} pairCompare;

struct pairCompareOperator1 {
  bool operator() (pair<int,double> i,pair<int,double> j) { return(i.second>j.second); }
} pairCompare1;


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
  ublas::matrix<double> a=ublas::zero_matrix<double>(ads.size()+queries.size(),ads.size()+queries.size());
	

  int posInQueries, posInAds;
  for(edgeIt=edges.begin();edgeIt!=edges.end();++edgeIt){
    posInQueries=std::find(queries.begin(), queries.end(),(*edgeIt).query)-queries.begin();
    posInAds=std::find(ads.begin(), ads.end(),(*edgeIt).ad)-ads.begin();
    a(posInQueries,posInAds+queries.size())=((double)(*edgeIt).numClicks)/maxClicks;
    a(posInAds+queries.size(),posInQueries)=((double)(*edgeIt).numClicks)/maxClicks;
  }
  //testputput
	
  // for(unsigned int i=0; i<a.size1();i++){
  //   for(unsigned int j=0; j<a.size2(); j++){
  //     cout<<a(j,i)<<"\t";
  //   }
  //   cout<<std::endl;
  // }
  // cout<<std::endl;

  //built transition Matrix p
  ublas::matrix<double> p=ublas::zero_matrix<double>(a.size1(),a.size1());
  for(unsigned int i=0; i<queries.size();i++){
    for(unsigned int j=queries.size(); j<p.size2(); j++){
      if(a(j,i)==0) continue;
      ublas::matrix_row<ublas::matrix<double> > row (a, j);
      ublas::matrix_column<ublas::matrix<double> > col (a, i);			
      p(j,i)=spread(row)*weight(col,j);			
    }
		
  }

  // XXX magick transition mode
  for(unsigned int i=queries.size(); i<p.size2();i++){
    for(unsigned int j=0; j<queries.size(); j++){
      if(a(j,i)==0) continue;
      ublas::matrix_row<ublas::matrix<double> > row (a, j);
      ublas::matrix_column<ublas::matrix<double> > col (a, i);			
      p(j,i)=spread(row)*weight(col,j);			
    }
  }
  // XXX magick transition mode

  for(unsigned int i=0; i<a.size1();i++){
    ublas::matrix_column<ublas::matrix<double> > col (p, i);	
    double sum=0;	
    for (unsigned j = 0; j < col.size(); ++j){
      sum+=col(j);		
    }
    p(i,i)=1.0-sum;
  }
	
  cout<<std::endl;
  for(unsigned int i=0; i<p.size1();i++){
    cout<<"|";
    for(unsigned int j=0; j<p.size2(); j++){
      cout<<p(i,j)<<"|";
    }
    cout<<std::endl;
  }
  cout<<std::endl;

  //build identity-matrix
  ublas::matrix<double> id=ublas::zero_matrix<int>(ads.size()+queries.size(),ads.size()+queries.size());
  for(unsigned int i=0; i<p.size1();i++)
    id(i,i)=1;
		
  //build V
  ublas::matrix<double> v=ublas::zero_matrix<int>(ads.size()+queries.size(),ads.size()+queries.size());
  for(unsigned int i=0; i<a.size1();i++){
    for(unsigned int j=0; j<a.size2(); j++){
      double sum=0;
      for(int h=1;h<=countSameNeighbors(a,i,j);++h)
	sum+=1.0/(pow(2,h));			
      v(i,j) = sum == 0 ? 0.25 : sum;
    }	
  }

  // cout<<std::endl;
  // for(unsigned int i=0; i<v.size1();i++){
  //   for(unsigned int j=0; j<v.size2(); j++){
  //     cout<<v(i,j)<<"\t";
  //   }
  //   cout<<std::endl;
  // }	

  ublas::matrix<double> s=id;
  int k=5;
  double c=0.8;
  //calc simrank with C=0.8, k=20
  for(int loop=0;loop<k;loop++){
    s = prod(c*p,s);
		
    s = prod(s,trans(p));
    //set diag=1	
    for(unsigned int col=0; col<s.size1();col++)
      for(unsigned int row=0; row<s.size2();row++)
	if(row==col)
	  s(col,row)=1;	
  }

  // XXX magick mode 
  for(unsigned int i=0; i<s.size1();i++)
    for(unsigned int j=0; j<s.size2(); j++)		
      s(i,j)=s(i,j)*v(i,j);
  // XXX magick mode

  // for(unsigned int i=0; i<s.size1();i++){
  //   for(unsigned int j=0; j<s.size2(); j++){
  //     cout<<s(i,j)<<"\t";
  //   }
  //   cout<<std::endl;
  // }
	

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
  std::cout << "|-" << std::endl;
  for(rankedRewritesIt=rankedRewrites.begin();rankedRewritesIt!=rankedRewrites.end();++rankedRewritesIt){
    cout<<"|"<<(*rankedRewritesIt).first<<"|";	
    for(simQueryIt=(*rankedRewritesIt).second.begin();simQueryIt!=(*rankedRewritesIt).second.end();++simQueryIt){
      cout<<(*simQueryIt).first<<"|"<<(*simQueryIt).second<<"|";				
    }		
    cout << endl;
  }
  std::cout << "|-" << std::endl;

	
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


/////////////////////// LDA FrontEnd //////////////////////////////

class ToCount
{
public:
  ToCount(std::multiset<std::string>* ctr) : ctr(ctr), count(0) { }
  std::string operator()(std::string& x) {
    std::string ret = "";
    int i = ctr->count(x);
    if(i==0){ 
      count++;
      return ret; 
    }
    ret.append(boost::lexical_cast<std::string>(count));
    ret.append(":");
    ret.append(boost::lexical_cast<std::string>(i));
    ret.append(" ");
    ++count;
    return ret;
  }
private:
  std::multiset<std::string>* ctr;
  int count;
};

bool tablesExist(boost::shared_ptr<sql::Connection> con) {
  try{
    scoped_ptr<sql::PreparedStatement> pstmt;
    scoped_ptr<sql::ResultSet> rs;

    pstmt.reset(con->prepareStatement("SELECT table_name "
				      "FROM information_schema.tables "
				      "WHERE table_schema = 'AdWorks' "
				      "AND table_name = 'Simularity';"));
    //you won't see it
    rs.reset(pstmt->executeQuery());
    return rs->next();
  } catch (sql::SQLException &e) {
    cout<<"in BackEnd::tablesExist: " <<e.getErrorCode()<<endl;
    throw e;
  }
} 

void lda(const std::string& path, std::ofstream& out, boost::shared_ptr<sql::Connection> con) {
  namespace fs = boost::filesystem;
  typedef std::vector<std::string> StrVec;
  typedef std::multiset<std::string> CountSet;
  typedef boost::tokenizer< boost::char_separator<char>,
                            std::istreambuf_iterator<char>
                            > Tokenizer;

  // throw away everything that we think is not part of the alphabet
  boost::char_separator<char> sep("  \".;:-()!?,\t\n–");
  StrVec global_strings;
  fs::path p(path);

  // collect the full dictionary
  StrVec dict;
  for(fs::directory_iterator it(p); it != fs::directory_iterator(); ++it) { 
    std::ifstream i(it->path().native().c_str());
    std::istreambuf_iterator<char> file_iter(i);
    std::istreambuf_iterator<char> eof;
    std::ostream_iterator<string> out_stream(out);
    Tokenizer tok(file_iter, eof, sep);
    // we want all unique words with their count appended to the string
    // read all words, sort them
    std::copy(tok.begin(), tok.end(), std::back_inserter(dict));
  }
  std::sort(dict.begin(), dict.end());
  dict.erase(std::unique(dict.begin(), dict.end()), dict.end());
  cout << dict.size() << endl;

  for(fs::directory_iterator it(p); it != fs::directory_iterator(); ++it) { 
    std::ifstream i(it->path().native().c_str());
    std::istreambuf_iterator<char> file_iter(i);
    std::istreambuf_iterator<char> eof;
    std::ostream_iterator<string> out_stream(out);
    StrVec vec;
    Tokenizer tok(file_iter, eof, sep);
    // we want all unique words with their count appended to the string
    // read all words, sort them
    std::copy(tok.begin(), tok.end(), std::back_inserter(vec));
    std::sort(vec.begin(), vec.end());

    CountSet counting_set(vec.begin(), vec.end());
    vec.erase(std::unique(vec.begin(), vec.end()), vec.end());

    out << vec.size() << " ";

    //decorate each with a count
    std::transform(dict.begin(), dict.end(), out_stream,
                   ToCount(&counting_set));
    out << "\n";
  }

  //std::system("lda est 0.025 40 settings.txt tmpfile-lda.txt random foo/");
  
  typedef tokenizer< boost::char_separator<char> > Tokenizer1;
  boost::char_separator<char> sep1(" ");
	
  ifstream in("foo/final.beta");
  if (!in.is_open()) throw;
  string line;	
  std::vector<std::pair<int,double> > clusterValues;
  int clusterNum=0;
  

  boost::scoped_ptr<sql::PreparedStatement> pstmt;
  if(tablesExist(con)) {
    //drop all
    try {
      pstmt.reset(con->prepareStatement("DROP TABLE Simularity"));
      pstmt->executeUpdate();
    } catch(sql::SQLException &e) {
      cout<<"in FrontEnd::initDatabase whiledropping: " <<e.getErrorCode()<<endl;
      throw e;
    } 
  }
  //create all
  try {
    pstmt.reset(con->prepareStatement("CREATE TABLE Simularity(Therm1 VARCHAR(60), Therm2 VARCHAR(60), PRIMARY KEY (Therm1, Therm2))"));
    pstmt->executeUpdate();
  } catch(sql::SQLException &e) {
    cout<<"in FrontEnd::initDatabase while creating: " << e.getErrorCode() << endl;
    throw e;
  } 
  
  while (getline(in,line)){	
    std::vector<std::pair<int,double> > clusterValues;
    Tokenizer1 tok(line,sep1);
    Tokenizer1::iterator tok_iter;
    std::vector<double>::iterator it;
    int count=0;
    for(tok_iter=tok.begin();tok_iter!=tok.end();++tok_iter){
       clusterValues.push_back(std::pair<int,double>(count,lexical_cast<double>(*tok_iter)));
       count++;
    }
    std::sort(clusterValues.begin(),clusterValues.end(),pairCompare1);
    std::vector<std::pair<int,double> >::iterator clusterIter=clusterValues.begin();
    
    cout << "Cluster " << clusterNum << ":" << endl;
    ++clusterNum;
    std::vector<std::string> simularStrs;
    for(int i=0;i<10;++i){
       cout<<(*clusterIter).second<<"\t"<<dict[(*clusterIter).first]<<endl;
       simularStrs.push_back(dict[(*clusterIter).first]);
       ++clusterIter;
    }
    for(int i=0;i<9;i++)
       for(int j=i+1;j<10;j++){
          try{
             pstmt.reset(con->prepareStatement("INSERT INTO Simularity(Therm1, Therm2) VALUES (?,?)"));
             pstmt->setString(1, simularStrs[i]);
	     pstmt->setString(2, simularStrs[j]);
             pstmt->executeUpdate();
          }catch(sql::SQLException &e) {
         }
       }
     for(int i=0;i<9;i++)
       for(int j=i+1;j<10;j++){
          try{
             pstmt.reset(con->prepareStatement("INSERT INTO Simularity(Therm1, Therm2) VALUES (?,?)"));
             pstmt->setString(1, simularStrs[j]);
	     pstmt->setString(2, simularStrs[i]);
             pstmt->executeUpdate();
          }catch(sql::SQLException &e) {
         }
       }
     
  }
}


std::vector<std::string> LDAFrontEnd::matchAd(const std::string& query,   
                                              const IUser*, bool*) /* ignored params */ {
  return std::vector<std::string>();
}
