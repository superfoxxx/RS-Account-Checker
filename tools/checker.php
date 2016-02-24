<?php
/*
	For the sake of releasing this, this is the checker.php script that would confirm somebody actually has a subscription on BA(using vBulletin) to use this.
	The strange line of md5(bcryptpass........()) stuff is due to the retarded/complex way of hashing password that is/was done on the forum.
*/
error_reporting(E_ALL);
ini_set('display_errors', 1);
require_once('./global.php');
error_reporting(E_ALL);
ini_set('display_errors', 1);

if(empty($_POST['email']) || empty($_POST['password']) || empty($_POST['hwid'])) {
        error_log("No POST");
        header("HTTP/1.1 403 Forbidden");
        exit;
}

$email = $db->escape_string($_POST['email']);
$hwid = $db->escape_string($_POST['hwid']);

$query = "SELECT salt FROM user WHERE MD5(LOWER(email)) = '".$email."';";
$result = $db->query_first($query);

if(empty($result['salt'])) {
        error_log("empty salt - " . $email);
        header("HTTP/1.1 403 Forbidden");
        exit;
}
$password = md5(bcrypt_passwd(md5(oldbcrypt(md5($_POST['password'] . $result['salt']), $result['salt'])), $result['salt']) . $result['salt']);


$query = "SELECT username FROM user WHERE MD5(email) = '".$email."' AND password = '".$password."' AND hwid = '".$hwid."'";
$result = $db->query_first($query);
if(!empty($result['username']))
        header("HTTP/1.1 200 OK");
else {
        error_log($email . "\t" . $password . "\t" . $hwid);
        header("HTTP/1.1 403 Forbidden");
}

exit;

