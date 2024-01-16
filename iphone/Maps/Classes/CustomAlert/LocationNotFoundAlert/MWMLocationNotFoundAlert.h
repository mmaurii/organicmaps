#import "MWMDefaultAlert.h"

@interface MWMLocationNotFoundAlert : MWMDefaultAlert

+ (instancetype)alertWithStopBlock:(MWMVoidBlock)okBlock;

@end
