/*
MySQL Data Transfer
Target Database: Hearthstone_accounts
Date: 03/02/2009 01:24:51
*/

-- ----------------------------
-- Table structure for accounts
-- ----------------------------
CREATE TABLE `accounts`(
  `acct` int(5) NOT NULL AUTO_INCREMENT ,
  `login` varchar(32) NOT NULL DEFAULT '' ,
  `password` varchar(255) NOT NULL DEFAULT '' ,
  `SessionKey` varchar(255) NOT NULL DEFAULT '' ,
  `gm` varchar(10) NOT NULL DEFAULT '0' ,
  `flags` int(11) NOT NULL DEFAULT 24 ,
  `banned` tinyint(1) NOT NULL DEFAULT 0 ,
  `lastlogin` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP ,
  `lastip` varchar(15) NOT NULL DEFAULT '' ,
  `forceLanguage` varchar(5) NOT NULL DEFAULT 'enUS' ,
  `email` varchar(32) DEFAULT NULL ,
  `muted` int(30) NOT NULL DEFAULT 0 ,
  PRIMARY KEY (`acct`),
  UNIQUE KEY `login` (`login`)
) ENGINE=MyISAM DEFAULT CHARSET=utf8;

-- ----------------------------
-- Table structure for ipbans
-- ----------------------------
CREATE TABLE `ipbans`(
  `ip` varchar(128) NOT NULL ,
  `time` int(30) NOT NULL DEFAULT 0 ,
  `expire` int(30) NOT NULL DEFAULT 0 ,
  PRIMARY KEY  (`ip`)
) ENGINE=MyISAM DEFAULT CHARSET=utf8;

