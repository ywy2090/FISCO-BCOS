pragma solidity >=0.4.19 <0.7.0;
pragma experimental ABIEncoderV2;

contract BenchAccount
{
    function createUser(string memory userID, string memory userName, string memory time) public returns(int256);
    function closeUser(string memory userID, string memory time) public returns(int256);
    function enableUser(string memory userID, string memory time) public returns(int256);
    function updateUserAddress(string memory userID, string memory addr, string memory time) public returns(int256);
    function updateUserPhone(string memory userID, string memory phone, string memory time) public returns(int256);
    function queryUserStatus(string memory userID) view public returns(int256, string memory);
    function queryUserState(string memory userID) view public returns(string memory/*address*/,string memory/*phone*/,string memory/*status*/,string memory/*time*/,string memory/*account list*/);

    function createAccount(string memory accountID, string memory userID, string memory time) public returns(int256);
    function enableAccount(string memory accountID, string memory time) public returns(int256);
    function freezeAccount(string memory accountID, string memory time) public returns(int256);
    function unfreezeAccount(string memory accountID,string memory time) public returns(int256);
    function closeAccount(string memory accountID, string memory time) public returns(int256);
    // userID,status,time,uint256,uint256
    function queryAccountState(string memory accountID) public view returns(int256, string memory/*user*/, string memory/*status*/, string memory/*time*/);
    function balance(string memory accountID) public view returns(int256, uint256);
    function deposit(string memory accountID, uint256 amount, string memory flowID, string memory time) public returns(int256);
    function withDraw(string memory accountID, uint256 amount, string memory flowID, string memory time) public returns(int256);
    function transfer(string memory fromAccountID, string[] memory toAccountID,uint256[] memory amount, string[] memory flowID, string memory time) public returns(int256);
    function transfer(string memory fromAccountID, string memory toAccountID,uint256 amount, string memory flowID, string memory time) public returns(int256);
    function queryAccountFlow(string memory accountID, string memory start, string memory end, uint256 page, uint256 limit)
public returns(int256, uint256, string[] memory);
}