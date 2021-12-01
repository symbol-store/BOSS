//JS module retrieving data from BOSS
//it manages UrlQueries and support graphic queries

var BOSSServerUrl = "";
var BOSSRestEndPoint = "";
var BOSSRestUrl = "";

var BOSSQueryInterval = 0;

export function setBOSSQueryInterval(myBOSSQueryInterval) {
    BOSSQueryInterval = myBOSSQueryInterval;
}
export function setBOSSUrl(myBOSSServerUrl, myBOSSRestEndPoint) {
    BOSSServerUrl = myBOSSServerUrl;
    BOSSRestEndPoint = myBOSSRestEndPoint;
    BOSSRestUrl = BOSSServerUrl + "/" + BOSSRestEndPoint + "/";
}

export function drawBOSSChart(drawFunction, updateFunction, queryUrl) {
    BOSSquery(queryUrl).then((data) => {
        drawFunction(data);
    }).then(() => {
        setInterval(function () {
            BOSSquery(queryUrl).then((data) => {
                updateFunction(data);
            });
        }, BOSSQueryInterval);
    });
}

export async function BOSSQuery(urlQuery) {
    if (urlQuery == "") {
        return new Promise((resolve, reject) => {
            reject("empty urlQuery.");
        });
    }
    var url = BOSSRestUrl + urlQuery;
    return new Promise((resolve, reject) => {
        fetch(url).then((res) => {
            res.json().then((data) => {
                data = JSON.stringify(data);
                resolve(data);
            },
                () => {
                    reject("request failed.");
                }
            );
        });
    });
}