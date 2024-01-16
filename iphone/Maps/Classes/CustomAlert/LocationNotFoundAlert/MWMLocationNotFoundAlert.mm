#import "MWMLocationNotFoundAlert.h"
#import "MWMDefaultAlert_Protected.h"
#import "MWMLocationManager.h"
#import "MWMLocationObserver.h"

@interface MWMLocationNotFoundAlert ()<MWMLocationObserver>

@end

@implementation MWMLocationNotFoundAlert

+ (instancetype)alertWithStopBlock:(MWMVoidBlock)stopBlock
{
  MWMLocationNotFoundAlert * alert =
      [self defaultAlertWithTitle:L(@"current_location_unknown_title")
                          message:L(@"current_location_unknown_message")
                 rightButtonTitle:L(@"current_location_unknown_continue_button")
                  leftButtonTitle:L(@"current_location_unknown_stop_button")
                rightButtonAction:nil
                 leftButtonAction:stopBlock
                              log:@"Location Not Found Alert"];
  [alert setNeedsCloseAlertAfterEnterBackground];
  [MWMLocationManager addObserver:alert];
  return alert;
}

#pragma mark - MWMLocationObserver

- (void)onLocationUpdate:(CLLocation *)location { [self close:self.rightButtonAction]; }
@end
